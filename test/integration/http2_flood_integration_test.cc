#include <algorithm>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/random_generator.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/network/socket_option_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/socket_interface_swap.h"
#include "test/integration/tracked_watermark_buffer.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::HasSubstr;

namespace Envoy {

namespace {
const uint32_t ControlFrameFloodLimit = 100;
const uint32_t AllFrameFloodLimit = 1000;

bool deferredProcessing(std::tuple<Network::Address::IpVersion, Http2Impl, bool> params) {
  return std::get<2>(params);
}

} // namespace

std::string testParamsToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, Http2Impl, bool>>
        params) {
  const Http2Impl http2_codec_impl = std::get<1>(params.param);
  return absl::StrCat(TestUtility::ipVersionToString(std::get<0>(params.param)),
                      http2_codec_impl == Http2Impl::Oghttp2 ? "Oghttp2" : "Nghttp2",
                      deferredProcessing(params.param) ? "WithDeferredProcessing"
                                                       : "NoDeferredProcessing");
}

// It is important that the new socket interface is installed before any I/O activity starts and
// the previous one is restored after all I/O activity stops. Since the HttpIntegrationTest
// destructor stops Envoy the SocketInterfaceSwap destructor needs to run after it. This order of
// multiple inheritance ensures that SocketInterfaceSwap destructor runs after
// Http2FrameIntegrationTest destructor completes.
// TODO(#28841) parameterize to run with and without UHV
class Http2FloodMitigationTest
    : public SocketInterfaceSwap,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, Http2Impl, bool>>,
      public Http2RawFrameIntegrationTest {
public:
  Http2FloodMitigationTest()
      : SocketInterfaceSwap(Network::Socket::Type::Stream),
        Http2RawFrameIntegrationTest(std::get<0>(GetParam())) {
    // This test tracks the number of buffers created, and the tag extraction check uses some
    // buffers, so disable it in this test.
    skip_tag_extraction_rule_check_ = true;

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(1); });
    config_helper_.addConfigModifier(configureProxyStatus());
    setupHttp2ImplOverrides(std::get<1>(GetParam()));
    config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                      deferredProcessing(GetParam()) ? "true" : "false");
  }

  void enableUhvRuntimeFeature() {
#ifdef ENVOY_ENABLE_UHV
    config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                      "true");
#endif
  }

protected:
  void initializeUpstreamFloodTest();
  std::vector<char> serializeFrames(const Http2Frame& frame, uint32_t num_frames);
  void floodServer(const Http2Frame& frame, const std::string& flood_stat, uint32_t num_frames);
  void floodServer(absl::string_view host, absl::string_view path,
                   Http2Frame::ResponseStatus expected_http_status, const std::string& flood_stat,
                   uint32_t num_frames);
  void floodClient(const Http2Frame& frame, uint32_t num_frames, const std::string& flood_stat);

  void setNetworkConnectionBufferSize();
  void beginSession() override;
  void prefillOutboundDownstreamQueue(uint32_t data_frame_count, uint32_t data_frame_size = 10);
  IntegrationStreamDecoderPtr prefillOutboundUpstreamQueue(uint32_t frame_count);
  void triggerListenerDrain();
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, Http2FloodMitigationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn({Http2Impl::Nghttp2, Http2Impl::Oghttp2}),
                     ::testing::Bool()),
    testParamsToString);

void Http2FloodMitigationTest::initializeUpstreamFloodTest() {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);
  // set lower upstream outbound frame limits to make tests run faster
  config_helper_.setUpstreamOutboundFramesLimits(AllFrameFloodLimit, ControlFrameFloodLimit);
  initialize();
}

void Http2FloodMitigationTest::setNetworkConnectionBufferSize() {
  // nghttp2 library has its own internal mitigation for outbound control frames (see
  // NGHTTP2_DEFAULT_MAX_OBQ_FLOOD_ITEM). The default nghttp2 mitigation threshold of 1K is modified
  // to 10K in the ConnectionImpl::Http2Options::Http2Options. The mitigation is triggered when
  // there are more than 10000 PING or SETTINGS frames with ACK flag in the nghttp2 internal
  // outbound queue. It is possible to trigger this mitigation in nghttp2 before triggering Envoy's
  // own flood mitigation. This can happen when a buffer large enough to contain over 10K PING or
  // SETTINGS frames is dispatched to the nghttp2 library. To prevent this from happening the
  // network connection receive buffer needs to be smaller than 90Kb (which is 10K SETTINGS frames).
  // Set it to the arbitrarily chosen value of 32K. Note that this buffer has 16K lower bound.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->listeners_size() >= 1, "");
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    listener->mutable_per_connection_buffer_limit_bytes()->set_value(32 * 1024);
  });
}

void Http2FloodMitigationTest::beginSession() {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);
  // set lower outbound frame limits to make tests run faster
  config_helper_.setDownstreamOutboundFramesLimits(AllFrameFloodLimit, ControlFrameFloodLimit);
  initialize();
  // Set up a raw connection to easily send requests without reading responses. Also, set a small
  // TCP receive buffer to speed up connection backup.
  auto options = std::make_shared<Network::Socket::Options>();
  options->emplace_back(std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_RCVBUF), 1024));
  write_matcher_->setSourcePort(lookupPort("http"));
  tcp_client_ = makeTcpConnection(lookupPort("http"), options);
  startHttp2Session();
}

std::vector<char> Http2FloodMitigationTest::serializeFrames(const Http2Frame& frame,
                                                            uint32_t num_frames) {
  std::vector<char> buf(num_frames * frame.size());
  for (auto pos = buf.begin(); pos != buf.end();) {
    pos = std::copy(frame.begin(), frame.end(), pos);
  }
  return buf;
}

// Verify that the server detects the flood of the given frame.
void Http2FloodMitigationTest::floodServer(const Http2Frame& frame, const std::string& flood_stat,
                                           uint32_t num_frames) {
  auto buf = serializeFrames(frame, num_frames);

  ASSERT_TRUE(tcp_client_->write({buf.begin(), buf.end()}, false, false));

  // Envoy's flood mitigation should kill the connection
  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter(flood_stat)->value());
  test_server_->waitForCounterGe("http.config_test.downstream_cx_delayed_close_timeout", 1);
}

// Send header only request, flood client, and verify that the upstream is disconnected and client
// receives 502.
void Http2FloodMitigationTest::floodClient(const Http2Frame& frame, uint32_t num_frames,
                                           const std::string& flood_stat) {
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Make Envoy's writes into the upstream connection to return EAGAIN
  write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());

  auto buf = serializeFrames(frame, num_frames);

  write_matcher_->setWriteReturnsEgain();
  auto* upstream = fake_upstreams_.front().get();
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  // Upstream connection should be disconnected
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  // Downstream client should receive 502 since upstream did not send response headers yet
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_EQ(response->headers().getProxyStatusValue(),
            "envoy; error=http_protocol_error; "
            "details=\"upstream_reset_before_response_started{protocol_error}; UPE\"");
  if (!flood_stat.empty()) {
    EXPECT_EQ(1, test_server_->counter(flood_stat)->value());
  }
}

// Verify that the server detects the flood using specified request parameters.
void Http2FloodMitigationTest::floodServer(absl::string_view host, absl::string_view path,
                                           Http2Frame::ResponseStatus expected_http_status,
                                           const std::string& flood_stat, uint32_t num_frames) {
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(request_idx), host, path);
  sendFrame(request);
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
  EXPECT_EQ(expected_http_status, frame.responseStatus());
  write_matcher_->setWriteReturnsEgain();
  for (uint32_t frame = 0; frame < num_frames; ++frame) {
    request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(++request_idx), host, path);
    sendFrame(request);
  }
  tcp_client_->waitForDisconnect();
  if (!flood_stat.empty()) {
    EXPECT_EQ(1, test_server_->counter(flood_stat)->value());
  }
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

void Http2FloodMitigationTest::prefillOutboundDownstreamQueue(uint32_t data_frame_count,
                                                              uint32_t data_frame_size) {
  // Set large buffer limits so the test is not affected by the flow control.
  config_helper_.setBufferLimits(1024 * 1024 * 1024, 1024 * 1024 * 1024);
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  beginSession();

  // Do not read from the socket and send request that causes autonomous upstream to respond
  // with the specified number of DATA frames. This pre-fills downstream outbound frame queue
  // such the next response triggers flood protection.
  // Simulate TCP push back on the Envoy's downstream network socket, so that outbound frames
  // start to accumulate in the transport socket buffer.
  write_matcher_->setWriteReturnsEgain();

  const auto request = Http2Frame::makeRequest(
      Http2Frame::makeClientStreamId(0), "host", "/test/long/url",
      {Http2Frame::Header("response_data_blocks", absl::StrCat(data_frame_count)),
       Http2Frame::Header("response_size_bytes", absl::StrCat(data_frame_size)),
       Http2Frame::Header("no_trailers", "0")});
  sendFrame(request);

  // Wait for some data to arrive and then wait for the upstream_rq_active to flip to 0 to indicate
  // that the first request has completed.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_rx_bytes_total", 10000);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("http2.outbound_frames_active", data_frame_count + 1);
  // Verify that pre-fill did not trigger flood protection
  EXPECT_EQ(0, test_server_->counter("http2.outbound_flood")->value());
}

IntegrationStreamDecoderPtr
Http2FloodMitigationTest::prefillOutboundUpstreamQueue(uint32_t frame_count) {
  // This complex exchange below is to ensure that the upstream outbound queue is empty before
  // forcing upstream socket to return EAGAIN. Envoy's upstream codec will send a few frames (i.e.
  // SETTINGS and ACKs) after a new stream was established, and the test needs to make sure these
  // are flushed into the socket. To do so the test goes through the following steps:
  // 1. send request headers, do not end stream
  // 2. wait for headers to be received by the upstream and send response headers without ending
  // the stream
  // 3. wait for the client to receive response headers
  // 4. send 1 byte of data from the client
  // 5. wait for 1 byte of data at the upstream. Receiving this DATA frame means that all other
  // frames that Envoy sent before it were also received by the upstream and the Envoy's upstream
  // outbound queue is empty.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  EXPECT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  EXPECT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  EXPECT_TRUE(upstream_request_->waitForHeadersComplete());

  upstream_request_->encodeHeaders(default_response_headers_, false);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().getStatusValue());
  codec_client_->sendData(*request_encoder_, 1, false);
  EXPECT_TRUE(upstream_request_->waitForData(*dispatcher_, 1));

  // Make Envoy's writes into the upstream connection to return EAGAIN
  write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());

  auto buf = serializeFrames(Http2Frame::makePingFrame(), frame_count);

  write_matcher_->setWriteReturnsEgain();
  auto* upstream = fake_upstreams_.front().get();
  EXPECT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));
  // Wait for pre-fill data to arrive to Envoy
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_rx_bytes_total", 500);
  // Verify that pre-fill did not kill upstream connection.
  EXPECT_TRUE(fake_upstream_connection_->connected());
  return response;
}

void Http2FloodMitigationTest::triggerListenerDrain() {
  absl::Notification drain_sequence_started;
  test_server_->server().dispatcher().post([this, &drain_sequence_started]() {
    test_server_->drainManager().startDrainSequence([] {});
    drain_sequence_started.Notify();
  });
  drain_sequence_started.WaitForNotification();
}

TEST_P(Http2FloodMitigationTest, Ping) {
  setNetworkConnectionBufferSize();
  beginSession();
  write_matcher_->setWriteReturnsEgain();
  floodServer(Http2Frame::makePingFrame(), "http2.outbound_control_flood",
              ControlFrameFloodLimit + 1);
}

TEST_P(Http2FloodMitigationTest, Settings) {
  setNetworkConnectionBufferSize();
  beginSession();
  write_matcher_->setWriteReturnsEgain();
  floodServer(Http2Frame::makeEmptySettingsFrame(), "http2.outbound_control_flood",
              ControlFrameFloodLimit + 1);
}

// Verify that the server can detect flood of internally generated 404 responses.
TEST_P(Http2FloodMitigationTest, 404) {
  // Change the default route to be restrictive, and send a request to a non existent route.
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  beginSession();

  // Send requests to a non existent path to generate 404s
  floodServer("host", "/notfound", Http2Frame::ResponseStatus::NotFound, "http2.outbound_flood",
              AllFrameFloodLimit + 1);
}

// Verify that the server can detect flood of response DATA frames
TEST_P(Http2FloodMitigationTest, Data) {
  auto buffer_factory = std::make_shared<Buffer::TrackedWatermarkBufferFactory>();
  setServerBufferFactory(buffer_factory);

  // Set large buffer limits so the test is not affected by the flow control.
  config_helper_.setBufferLimits(1024 * 1024 * 1024, 1024 * 1024 * 1024);
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  beginSession();

  // Do not read from the socket and send request that causes autonomous upstream
  // to respond with 1000 DATA frames. The Http2FloodMitigationTest::beginSession()
  // sets 1000 flood limit for all frame types. Including 1 HEADERS response frame
  // 1000 DATA frames should trigger flood protection.
  // Simulate TCP push back on the Envoy's downstream network socket, so that outbound frames start
  // to accumulate in the transport socket buffer.
  write_matcher_->setWriteReturnsEgain();

  const auto request = Http2Frame::makeRequest(1, "host", "/test/long/url",
                                               {Http2Frame::Header("response_data_blocks", "1000"),
                                                Http2Frame::Header("response_size_bytes", "1"),
                                                Http2Frame::Header("no_trailers", "0")});
  sendFrame(request);

  // Wait for connection to be flooded with outbound DATA frames and disconnected.
  tcp_client_->waitForDisconnect();

  // If the server codec had incorrectly thrown an exception on flood detection it would cause
  // the entire upstream to be disconnected. Verify it is still active, and there are no destroyed
  // connections.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
  // The factory will be used to create 4 buffers: the input and output buffers for request and
  // response pipelines.
  EXPECT_EQ(8, buffer_factory->numBuffersCreated());

  // Expect at least 1000 1 byte data frames in the output buffer. Each data frame comes with a
  // 9-byte frame header; 10 bytes per data frame, 10000 bytes total. The output buffer should also
  // contain response headers, which should be less than 100 bytes.
  EXPECT_LE(10000, buffer_factory->maxBufferSize());
  EXPECT_GE(10100, buffer_factory->maxBufferSize());

  // The response pipeline input buffer could end up with the full upstream response in 1 go, but
  // there are no guarantees of that being the case.
  EXPECT_LE(10000, buffer_factory->sumMaxBufferSizes());
  // The max size of the input and output buffers used in the response pipeline is around 10kb each.
  EXPECT_GE(22000, buffer_factory->sumMaxBufferSizes());
  // Verify that all buffers have watermarks set.
  EXPECT_THAT(buffer_factory->highWatermarkRange(),
              testing::Pair(256 * 1024 * 1024, 1024 * 1024 * 1024));
}

// Verify that the server can detect flood triggered by a DATA frame from a decoder filter call
// to sendLocalReply().
// This test also verifies that RELEASE_ASSERT in the ConnectionImpl::StreamImpl::encodeDataHelper()
// is not fired when it is called by the sendLocalReply() in the dispatching context.
TEST_P(Http2FloodMitigationTest, DataOverflowFromDecoderFilterSendLocalReply) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        const std::string yaml_string = R"EOF(
name: send_local_reply_filter
typed_config:
  "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
  prefix: "/call_send_local_reply"
  code: 404
  body: "something"
  )EOF";
        TestUtility::loadFromYaml(yaml_string, *hcm.add_http_filters());
        // keep router the last
        auto size = hcm.http_filters_size();
        hcm.mutable_http_filters()->SwapElements(size - 2, size - 1);
      });

  // pre-fill 2 away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 2);

  // At this point the outbound downstream frame queue should be 2 away from overflowing.
  // Make the SetResponseCodeFilterConfig decoder filter call sendLocalReply with body.
  // HEADERS + DATA frames should overflow the queue.
  // Verify that connection was disconnected and appropriate counters were set.
  auto request2 =
      Http2Frame::makeRequest(Http2Frame::makeClientStreamId(1), "host", "/call_send_local_reply");
  sendFrame(request2);

  // Wait for connection to be flooded with outbound DATA frame and disconnected.
  tcp_client_->waitForDisconnect();

  // Verify that the upstream connection is still alive.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// Verify that the server can detect flood of response HEADERS frames
TEST_P(Http2FloodMitigationTest, Headers) {
  // pre-fill one away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 1);

  // Send second request which should trigger headers only response.
  // Verify that connection was disconnected and appropriate counters were set.
  auto request2 = Http2Frame::makeRequest(
      Http2Frame::makeClientStreamId(1), "host", "/test/long/url",
      {Http2Frame::Header("response_data_blocks", "0"), Http2Frame::Header("no_trailers", "0")});
  sendFrame(request2);

  // Wait for connection to be flooded with outbound HEADERS frame and disconnected.
  tcp_client_->waitForDisconnect();

  // If the server codec had incorrectly thrown an exception on flood detection it would cause
  // the entire upstream to be disconnected. Verify it is still active, and there are no destroyed
  // connections.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// Verify that the server can detect overflow by 100 continue response sent by Envoy itself
TEST_P(Http2FloodMitigationTest, Envoy1xxHeaders) {
  // pre-fill one away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 1);

  // Send second request which should trigger Envoy to respond with 100 continue.
  // Verify that connection was disconnected and appropriate counters were set.
  auto request2 = Http2Frame::makeRequest(
      Http2Frame::makeClientStreamId(1), "host", "/test/long/url",
      {Http2Frame::Header("response_data_blocks", "0"), Http2Frame::Header("no_trailers", "0"),
       Http2Frame::Header("expect", "100-continue")});
  sendFrame(request2);

  // Wait for connection to be flooded with outbound HEADERS frame and disconnected.
  tcp_client_->waitForDisconnect();

  // If the server codec had incorrectly thrown an exception on flood detection it would cause
  // the entire upstream to be disconnected. Verify it is still active, and there are no destroyed
  // connections.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // The second upstream request should be reset since it is disconnected when sending 100 continue
  // response
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_tx_reset")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// Verify that the server can detect flood triggered by a HEADERS frame from a decoder filter call
// to sendLocalReply().
// This test also verifies that RELEASE_ASSERT in the
// ConnectionImpl::StreamImpl::encodeHeadersBase() is not fired when it is called by the
// sendLocalReply() in the dispatching context.
TEST_P(Http2FloodMitigationTest, HeadersOverflowFromDecoderFilterSendLocalReply) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        const std::string yaml_string = R"EOF(
name: send_local_reply_filter
typed_config:
  "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
  prefix: "/call_send_local_reply"
  code: 404
  )EOF";
        TestUtility::loadFromYaml(yaml_string, *hcm.add_http_filters());
        // keep router the last
        auto size = hcm.http_filters_size();
        hcm.mutable_http_filters()->SwapElements(size - 2, size - 1);
      });

  // pre-fill one away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 1);

  // At this point the outbound downstream frame queue should be 1 away from overflowing.
  // Make the SetResponseCodeFilterConfig decoder filter call sendLocalReply without body.
  // Verify that connection was disconnected and appropriate counters were set.
  auto request2 =
      Http2Frame::makeRequest(Http2Frame::makeClientStreamId(1), "host", "/call_send_local_reply");
  sendFrame(request2);

  // Wait for connection to be flooded with outbound HEADERS frame and disconnected.
  tcp_client_->waitForDisconnect();

  // Verify that the upstream connection is still alive.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// TODO(yanavlasov): add the same tests as above for the encoder filters.
// This is currently blocked by the https://github.com/envoyproxy/envoy/pull/13256

// Verify that the server can detect flood of response METADATA frames
TEST_P(Http2FloodMitigationTest, Metadata) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->set_allow_metadata(true);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  // pre-fill one away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 1);

  // Send second request which should trigger response with METADATA frame.
  auto metadata_map_vector_ptr = std::make_unique<Http::MetadataMapVector>();
  Http::MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
      {"header_key2", "header_value2"},
  };
  auto metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  metadata_map_vector_ptr->push_back(std::move(metadata_map_ptr));
  static_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setPreResponseHeadersMetadata(std::move(metadata_map_vector_ptr));

  // Verify that connection was disconnected and appropriate counters were set.
  auto request2 = Http2Frame::makeRequest(
      Http2Frame::makeClientStreamId(1), "host", "/test/long/url",
      {Http2Frame::Header("response_data_blocks", "0"), Http2Frame::Header("no_trailers", "0")});
  sendFrame(request2);

  // Wait for connection to be flooded with outbound METADATA frame and disconnected.
  tcp_client_->waitForDisconnect();

  // If the server codec had incorrectly thrown an exception on flood detection it would cause
  // the entire upstream to be disconnected. Verify it is still active, and there are no destroyed
  // connections.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// Verify that the server can detect flood of response trailers.
TEST_P(Http2FloodMitigationTest, Trailers) {
  // Set large buffer limits so the test is not affected by the flow control.
  config_helper_.setBufferLimits(1024 * 1024 * 1024, 1024 * 1024 * 1024);
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  beginSession();

  // Do not read from the socket and send request that causes autonomous upstream
  // to respond with 999 DATA frames and trailers. The Http2FloodMitigationTest::beginSession()
  // sets 1000 flood limit for all frame types. Including 1 HEADERS response frame
  // 999 DATA frames and trailers should trigger flood protection.
  // Simulate TCP push back on the Envoy's downstream network socket, so that outbound frames start
  // to accumulate in the transport socket buffer.
  write_matcher_->setWriteReturnsEgain();

  static_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseTrailers(std::make_unique<Http::TestResponseTrailerMapImpl>(
          Http::TestResponseTrailerMapImpl({{"foo", "bar"}})));

  const auto request =
      Http2Frame::makeRequest(Http2Frame::makeClientStreamId(0), "host", "/test/long/url",
                              {Http2Frame::Header("response_data_blocks", "999")});
  sendFrame(request);

  // Wait for connection to be flooded with outbound trailers and disconnected.
  tcp_client_->waitForDisconnect();

  // If the server codec had incorrectly thrown an exception on flood detection it would cause
  // the entire upstream to be disconnected. Verify it is still active, and there are no destroyed
  // connections.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// Verify flood detection by the WINDOW_UPDATE frame when a decoder filter is resuming reading from
// the downstream via DecoderFilterBelowWriteBufferLowWatermark.
TEST_P(Http2FloodMitigationTest, WindowUpdateOnLowWatermarkFlood) {
  // This test depends on data flowing through a backed up stream eagerly (e.g. the
  // backpressure-filter triggers above watermark when it receives headers from
  // the downstream, and only goes below watermark if the response body has
  // passed through the filter.). With defer processing of backed up streams however
  // the data won't be eagerly processed as the stream is backed up.
  // TODO(kbaichoo): Remove this test when removing this feature tag.
  if (deferredProcessing(GetParam())) {
    return;
  }
  config_helper_.prependFilter(R"EOF(
  name: backpressure-filter
  )EOF");
  config_helper_.setBufferLimits(1024 * 1024 * 1024, 1024 * 1024 * 1024);
  // Set low window sizes in the server codec as nghttp2 sends WINDOW_UPDATE only after it consumes
  // more than 25% of the window.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* h2_options = hcm.mutable_http2_protocol_options();
        h2_options->mutable_initial_stream_window_size()->set_value(70000);
        h2_options->mutable_initial_connection_window_size()->set_value(70000);
      });
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  beginSession();

  write_matcher_->setWriteReturnsEgain();

  // pre-fill two away from overflow
  const auto request = Http2Frame::makePostRequest(
      Http2Frame::makeClientStreamId(0), "host", "/test/long/url",
      {Http2Frame::Header("response_data_blocks", "998"), Http2Frame::Header("no_trailers", "0")});
  sendFrame(request);

  // The backpressure-filter disables reading when it sees request headers, and it should prevent
  // WINDOW_UPDATE to be sent on the following DATA frames. Send enough DATA to consume more than
  // 25% of the 70K window so that nghttp2 will send WINDOW_UPDATE on read resumption.
  auto data_frame =
      Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(0), std::string(16384, '0'));
  sendFrame(data_frame);
  sendFrame(data_frame);
  data_frame = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(0), std::string(16384, '1'),
                                         Http2Frame::DataFlags::EndStream);
  sendFrame(data_frame);

  // Upstream will respond with 998 DATA frames and the backpressure-filter filter will re-enable
  // reading on the last DATA frame. This will cause nghttp2 to send two WINDOW_UPDATE frames for
  // stream and connection windows. Together with response DATA frames it should overflow outbound
  // frame queue. Wait for connection to be flooded with outbound WINDOW_UPDATE frame and
  // disconnected.
  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_flow_control_paused_reading_total")
                ->value());

  // If the server codec had incorrectly thrown an exception on flood detection it would cause
  // the entire upstream to be disconnected. Verify it is still active, and there are no destroyed
  // connections.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// TODO(yanavlasov): add tests for WINDOW_UPDATE overflow from the router filter. These tests need
// missing support for write resumption from test sockets that were forced to return EAGAIN by the
// test.

// Verify that the server can detect flood of RST_STREAM frames.
TEST_P(Http2FloodMitigationTest, RST_STREAM) {
#ifdef ENVOY_ENABLE_UHV
  // TODO(#27541): the invalid frame that used to cause Envoy to send only RST_STREAM now causes
  // Envoy to also send 400 in UHV mode (it is allowed by RFC). The test needs to be fixed to make
  // server only send RST_STREAM.
  if (std::get<1>(GetParam()) == Http2Impl::Oghttp2) {
    return;
  }
#endif
  // Use invalid HTTP headers to trigger sending RST_STREAM frames.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_http2_protocol_options()
            ->mutable_override_stream_error_on_invalid_http_message()
            ->set_value(true);
      });
  beginSession();

  uint32_t stream_index = 0;
  auto request =
      Http::Http2::Http2Frame::makeMalformedRequest(Http2Frame::makeClientStreamId(stream_index));
  sendFrame(request);
  auto response = readFrame();
  // Make sure we've got RST_STREAM from the server
  EXPECT_EQ(Http2Frame::Type::RstStream, response.type());

  // Simulate TCP push back on the Envoy's downstream network socket, so that outbound frames start
  // to accumulate in the transport socket buffer.
  write_matcher_->setWriteReturnsEgain();

  for (++stream_index; stream_index < ControlFrameFloodLimit + 2; ++stream_index) {
    request =
        Http::Http2::Http2Frame::makeMalformedRequest(Http2Frame::makeClientStreamId(stream_index));
    sendFrame(request);
  }
  tcp_client_->waitForDisconnect();
  EXPECT_EQ(1, test_server_->counter("http2.outbound_control_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

// Verify detection of flood by the RST_STREAM frame sent on pending flush timeout
TEST_P(Http2FloodMitigationTest, RstStreamOverflowOnPendingFlushTimeout) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.mutable_stream_idle_timeout()->set_seconds(0);
        constexpr uint64_t IdleTimeoutMs = 400;
        hcm.mutable_stream_idle_timeout()->set_nanos(IdleTimeoutMs * 1000 * 1000);
      });

  // Pending flush timer is started when upstream response has completed but there is no window to
  // send DATA downstream. The test downstream client does not update WINDOW and as such Envoy will
  // use the default 65535 bytes. First, pre-fill outbound queue with 65 byte frames, which should
  // consume 65 * 997 = 64805 bytes of downstream connection window.
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 3, 65);

  // At this point the outbound downstream frame queue should be 3 away from overflowing with 730
  // byte window. Make response to be 1 DATA frame with 1024 payload. This should overflow the
  // available downstream window and start pending flush timer. Envoy proxies 2 frames downstream,
  // HEADERS and partial DATA frame, which makes the frame queue 1 away from overflow.
  const auto request2 = Http2Frame::makeRequest(
      Http2Frame::makeClientStreamId(1), "host", "/test/long/url",
      {Http2Frame::Header("response_data_blocks", "1"),
       Http2Frame::Header("response_size_bytes", "1024"), Http2Frame::Header("no_trailers", "0")});
  sendFrame(request2);

  // Pending flush timer sends RST_STREAM frame which should overflow outbound frame queue and
  // disconnect the connection.
  tcp_client_->waitForDisconnect();

  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
  // Verify that pending flush timeout was hit
  EXPECT_EQ(1, test_server_->counter("http2.tx_flush_timeout")->value());
}

// Verify detection of frame flood when sending second GOAWAY frame on drain timeout
TEST_P(Http2FloodMitigationTest, GoAwayOverflowOnDrainTimeout) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* drain_time_out = hcm.mutable_drain_timeout();
        std::chrono::milliseconds timeout(1000);
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        drain_time_out->set_seconds(seconds.count());

        auto* http_protocol_options = hcm.mutable_common_http_protocol_options();
        auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
        idle_time_out->set_seconds(seconds.count());
      });
  // pre-fill two away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 2);

  // connection idle timeout will send first GOAWAY frame and start drain timer
  // drain timeout will send second GOAWAY frame which should trigger flood protection
  // Wait for connection to be flooded with outbound GOAWAY frame and disconnected.
  tcp_client_->waitForDisconnect();

  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// Verify detection of overflowing outbound frame queue with the GOAWAY frames sent after the
// downstream idle connection timeout disconnects the connection.
// The test verifies protocol constraint violation handling in the
// Http2::ConnectionImpl::shutdownNotice() method.
TEST_P(Http2FloodMitigationTest, DownstreamIdleTimeoutTriggersFloodProtection) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* http_protocol_options = hcm.mutable_common_http_protocol_options();
        auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
        std::chrono::milliseconds timeout(1000);
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        idle_time_out->set_seconds(seconds.count());
      });

  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 1);
  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_cx_idle_timeout")->value());
}

// Verify detection of overflowing outbound frame queue with the GOAWAY frames sent after the
// downstream connection duration timeout disconnects the connection.
// The test verifies protocol constraint violation handling in the
// Http2::ConnectionImpl::shutdownNotice() method.
TEST_P(Http2FloodMitigationTest, DownstreamConnectionDurationTimeoutTriggersFloodProtection) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* http_protocol_options = hcm.mutable_common_http_protocol_options();
        auto* max_connection_duration = http_protocol_options->mutable_max_connection_duration();
        std::chrono::milliseconds timeout(1000);
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        max_connection_duration->set_seconds(seconds.count());
      });
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 1);
  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_max_duration_reached")->value());
}

// Verify detection of frame flood when sending GOAWAY frame during processing of response headers
// on a draining listener.
TEST_P(Http2FloodMitigationTest, GoawayOverflowDuringResponseWhenDraining) {
  // pre-fill one away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 1);

  triggerListenerDrain();

  // Send second request which should trigger Envoy to send GOAWAY (since it is in the draining
  // state) when processing response headers. Verify that connection was disconnected and
  // appropriate counters were set.
  auto request2 =
      Http2Frame::makeRequest(Http2Frame::makeClientStreamId(1), "host", "/test/long/url");
  sendFrame(request2);

  // Wait for connection to be flooded with outbound GOAWAY frame and disconnected.
  tcp_client_->waitForDisconnect();

  // Verify that the upstream connection is still alive.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_cx_drain_close")->value());
}

// Verify detection of frame flood when sending GOAWAY frame during call to sendLocalReply()
// from decoder filter on a draining listener.
TEST_P(Http2FloodMitigationTest, GoawayOverflowFromDecoderFilterSendLocalReplyWhenDraining) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        const std::string yaml_string = R"EOF(
name: send_local_reply_filter
typed_config:
  "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
  prefix: "/call_send_local_reply"
  code: 404
  )EOF";
        TestUtility::loadFromYaml(yaml_string, *hcm.add_http_filters());
        // keep router the last
        auto size = hcm.http_filters_size();
        hcm.mutable_http_filters()->SwapElements(size - 2, size - 1);
      });

  // pre-fill one away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 1);

  triggerListenerDrain();

  // At this point the outbound downstream frame queue should be 1 away from overflowing.
  // Make the SetResponseCodeFilterConfig decoder filter call sendLocalReply without body which
  // should trigger Envoy to send GOAWAY (since it is in the draining state) when processing
  // sendLocalReply() headers. Verify that connection was disconnected and appropriate counters were
  // set.
  auto request2 =
      Http2Frame::makeRequest(Http2Frame::makeClientStreamId(1), "host", "/call_send_local_reply");
  sendFrame(request2);

  // Wait for connection to be flooded with outbound GOAWAY frame and disconnected.
  tcp_client_->waitForDisconnect();

  // Verify that the upstream connection is still alive.
  ASSERT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
  ASSERT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_cx_drain_close")->value());
}

// Verify that the server can detect flooding by the RST_STREAM on when upstream disconnects
// before sending response headers.
TEST_P(Http2FloodMitigationTest, RstStreamOnUpstreamRemoteCloseBeforeResponseHeaders) {
  // pre-fill 3 away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 3);

  // Start second request.
  auto request2 =
      Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(1), "host", "/test/long/url");
  sendFrame(request2);

  // Wait for it to be proxied
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_total", 2);

  // Disconnect upstream connection. Since there no response headers were sent yet the router
  // filter will send 503 with body and then RST_STREAM. With these 3 frames the downstream outbound
  // frame queue should overflow.
  ASSERT_TRUE(static_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->closeConnection(0));

  // Wait for connection to be flooded with outbound RST_STREAM frame and disconnected.
  tcp_client_->waitForDisconnect();

  ASSERT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_destroy")->value());
  // Verify that the flood check was triggered
  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// Verify that the server can detect flooding by the RST_STREAM on stream idle timeout
// after sending response headers.
TEST_P(Http2FloodMitigationTest, RstStreamOnStreamIdleTimeoutAfterResponseHeaders) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* stream_idle_timeout = hcm.mutable_stream_idle_timeout();
        std::chrono::milliseconds timeout(1000);
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        stream_idle_timeout->set_seconds(seconds.count());
      });
  // pre-fill 2 away from overflow
  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 2);

  // Start second request, which should result in response headers to be sent but the stream kept
  // open.
  auto request2 = Http2Frame::makeRequest(
      Http2Frame::makeClientStreamId(1), "host", "/test/long/url",
      {Http2Frame::Header("response_data_blocks", "0"), Http2Frame::Header("no_end_stream", "0")});
  sendFrame(request2);

  // Wait for stream idle timeout to send RST_STREAM. With the response headers frame from the
  // second response the downstream outbound frame queue should overflow.
  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_idle_timeout")->value());
}

// Verify detection of overflowing outbound frame queue with the PING frames sent by the keep alive
// timer. The test verifies protocol constraint violation handling in the
// Http2::ConnectionImpl::sendKeepalive() method.
TEST_P(Http2FloodMitigationTest, KeepAliveTimerTriggersFloodProtection) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* keep_alive = hcm.mutable_http2_protocol_options()->mutable_connection_keepalive();
        keep_alive->mutable_interval()->set_seconds(1 * TSAN_TIMEOUT_FACTOR);
        keep_alive->mutable_timeout()->set_seconds(2 * TSAN_TIMEOUT_FACTOR);
      });

  prefillOutboundDownstreamQueue(AllFrameFloodLimit - 1);
  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.outbound_flood")->value());
}

// Verify that the server stop reading downstream connection on protocol error.
TEST_P(Http2FloodMitigationTest, TooManyStreams) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_http2_protocol_options()->mutable_max_concurrent_streams()->set_value(2);
      });
  autonomous_upstream_ = true;
  beginSession();
  // To prevent Envoy from closing client streams the upstream connection needs to push back on
  // writing by the upstream server. In this case Envoy will not see upstream responses and will
  // keep client streams open, eventually maxing them out and causing client connection to be
  // closed.
  write_matcher_->setSourcePort(fake_upstreams_[0]->localAddress()->ip()->port());

  // Exceed the number of streams allowed by the server. The server should stop reading from the
  // client.
  floodServer("host", "/test/long/url", Http2Frame::ResponseStatus::Ok, "", 3);
}

TEST_P(Http2FloodMitigationTest, EmptyHeaders) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()
            ->mutable_max_consecutive_inbound_frames_with_empty_payload()
            ->set_value(0);
      });
  beginSession();

  const auto request = Http2Frame::makeEmptyHeadersFrame(Http2Frame::makeClientStreamId(0));
  sendFrame(request);

  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.inbound_empty_frames_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

TEST_P(Http2FloodMitigationTest, EmptyHeadersContinuation) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  beginSession();

  const uint32_t request_stream_id = Http2Frame::makeClientStreamId(0);
  auto request = Http2Frame::makeEmptyHeadersFrame(request_stream_id);
  sendFrame(request);

  for (int i = 0; i < 2; i++) {
    request = Http2Frame::makeEmptyContinuationFrame(request_stream_id);
    sendFrame(request);
  }

  tcp_client_->waitForDisconnect();

  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("http2.inbound_empty_frames_flood"));
  EXPECT_EQ(1, test_server_->counter("http2.inbound_empty_frames_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

TEST_P(Http2FloodMitigationTest, EmptyData) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  beginSession();

  const uint32_t request_stream_id = Http2Frame::makeClientStreamId(0);
  auto request = Http2Frame::makePostRequest(request_stream_id, "host", "/");
  sendFrame(request);

  for (int i = 0; i < 2; i++) {
    request = Http2Frame::makeEmptyDataFrame(request_stream_id);
    sendFrame(request);
  }

  tcp_client_->waitForDisconnect();

  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("http2.inbound_empty_frames_flood"));
  EXPECT_EQ(1, test_server_->counter("http2.inbound_empty_frames_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

TEST_P(Http2FloodMitigationTest, PriorityIdleStream) {
  beginSession();

  floodServer(Http2Frame::makePriorityFrame(Http2Frame::makeClientStreamId(0),
                                            Http2Frame::makeClientStreamId(1)),
              "http2.inbound_priority_frames_flood",
              Http2::Utility::OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM + 1);
}

TEST_P(Http2FloodMitigationTest, PriorityOpenStream) {
  beginSession();

  // Open stream.
  const uint32_t request_stream_id = Http2Frame::makeClientStreamId(0);
  const auto request = Http2Frame::makeRequest(request_stream_id, "host", "/");
  sendFrame(request);

  floodServer(Http2Frame::makePriorityFrame(request_stream_id, Http2Frame::makeClientStreamId(1)),
              "http2.inbound_priority_frames_flood",
              Http2::Utility::OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM * 2 +
                  1);
}

TEST_P(Http2FloodMitigationTest, PriorityClosedStream) {
  autonomous_upstream_ = true;
  beginSession();

  // Open stream.
  const uint32_t request_stream_id = Http2Frame::makeClientStreamId(0);
  const auto request = Http2Frame::makeRequest(request_stream_id, "host", "/");
  sendFrame(request);
  // Reading response marks this stream as closed in nghttp2.
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());

  floodServer(Http2Frame::makePriorityFrame(request_stream_id, Http2Frame::makeClientStreamId(1)),
              "http2.inbound_priority_frames_flood",
              Http2::Utility::OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM * 2 +
                  1);
}

TEST_P(Http2FloodMitigationTest, WindowUpdate) {
  beginSession();

  // Open stream.
  const uint32_t request_stream_id = Http2Frame::makeClientStreamId(0);
  const auto request = Http2Frame::makeRequest(request_stream_id, "host", "/");
  sendFrame(request);

  // See the limit formula in the
  // `Envoy::Http::Http2::ProtocolConstraints::checkInboundFrameLimits()' method.
  constexpr uint32_t max_allowed =
      5 + 2 * (1 + Http2::Utility::OptionsLimits::
                           DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT *
                       0 /* 0 DATA frames */);
  floodServer(Http2Frame::makeWindowUpdateFrame(request_stream_id, 1),
              "http2.inbound_window_update_frames_flood", max_allowed + 1);
}

// Verify that the HTTP/2 connection is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, ZerolenHeader) {
  enableUhvRuntimeFeature();
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  beginSession();

  // Send invalid request.
  const auto request = Http2Frame::makeMalformedRequestWithZerolenHeader(
      Http2Frame::makeClientStreamId(0), "host", "/");
  sendFrame(request);

  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.rx_messaging_error")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("header"));
  // expect a downstream protocol error.
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("DPE"));
}

// Verify that only the offending stream is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, ZerolenHeaderAllowed) {
#ifdef ENVOY_ENABLE_UHV
  // TODO(#27541): the invalid frame that used to cause Envoy to send only RST_STREAM now causes
  // Envoy to also send 400 in UHV mode (it is allowed by RFC). The test needs to be fixed to make
  // server only send RST_STREAM.
  if (std::get<1>(GetParam()) == Http2Impl::Oghttp2) {
    return;
  }
#endif
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_http2_protocol_options()
            ->mutable_override_stream_error_on_invalid_http_message()
            ->set_value(true);
      });
  autonomous_upstream_ = true;
  beginSession();

  // Send invalid request.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeMalformedRequestWithZerolenHeader(
      Http2Frame::makeClientStreamId(request_idx), "host", "/");
  sendFrame(request);
  // Make sure we've got RST_STREAM from the server.
  auto response = readFrame();
  EXPECT_EQ(Http2Frame::Type::RstStream, response.type());

  // Send valid request using the same connection.
  request_idx++;
  request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(request_idx), "host", "/");
  sendFrame(request);
  response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, response.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, response.responseStatus());

  tcp_client_->close();

  EXPECT_EQ(1, test_server_->counter("http2.rx_messaging_error")->value());
  EXPECT_EQ(0,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
  // expect Downstream Protocol Error
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0, true), HasSubstr("header"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0, true), HasSubstr("DPE"));
}

TEST_P(Http2FloodMitigationTest, UpstreamPingFlood) {
  initializeUpstreamFloodTest();
  floodClient(Http2Frame::makePingFrame(), ControlFrameFloodLimit + 1,
              "cluster.cluster_0.http2.outbound_control_flood");
}

TEST_P(Http2FloodMitigationTest, UpstreamSettings) {
  initializeUpstreamFloodTest();
  floodClient(Http2Frame::makeEmptySettingsFrame(), ControlFrameFloodLimit + 1,
              "cluster.cluster_0.http2.outbound_control_flood");
}

TEST_P(Http2FloodMitigationTest, UpstreamWindowUpdate) {
  initializeUpstreamFloodTest();
  constexpr uint32_t max_allowed =
      5 + 2 * (1 + Http2::Utility::OptionsLimits::
                           DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT *
                       0 /* 0 DATA frames */);
  floodClient(Http2Frame::makeWindowUpdateFrame(0, 1), max_allowed + 1,
              "cluster.cluster_0.http2.inbound_window_update_frames_flood");
}

TEST_P(Http2FloodMitigationTest, UpstreamEmptyHeaders) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->mutable_max_consecutive_inbound_frames_with_empty_payload()
        ->set_value(0);
    ConfigHelper::setProtocolOptions(*cluster, protocol_options);
  });
  initializeUpstreamFloodTest();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  auto* upstream = fake_upstreams_.front().get();
  auto buf = Http2Frame::makeEmptyHeadersFrame(Http2Frame::makeClientStreamId(0),
                                               Http2Frame::HeadersFlags::None);
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_EQ(1,
            test_server_->counter("cluster.cluster_0.http2.inbound_empty_frames_flood")->value());
}

// Verify that the HTTP/2 connection is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, UpstreamZerolenHeader) {
  enableUhvRuntimeFeature();
  initializeUpstreamFloodTest();
  // Send client request which will send an upstream request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send an upstream reply.
  auto* upstream = fake_upstreams_.front().get();
  const auto buf =
      Http2Frame::makeMalformedResponseWithZerolenHeader(Http2Frame::makeClientStreamId(0));
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.rx_messaging_error")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_protocol_error")->value());
  EXPECT_EQ("502", response->headers().getStatusValue());
}

// Verify that the HTTP/2 connection is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, UpstreamZerolenHeaderAllowed) {
  enableUhvRuntimeFeature();
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->mutable_override_stream_error_on_invalid_http_message()
        ->set_value(1);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  initializeUpstreamFloodTest();

  // Send client request which will send an upstream request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send an upstream reply.
  auto* upstream = fake_upstreams_.front().get();
  const auto buf =
      Http2Frame::makeMalformedResponseWithZerolenHeader(Http2Frame::makeClientStreamId(0));
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  // Make sure upstream and downstream got RST_STREAM from the server.
  ASSERT_TRUE(upstream_request_->waitForReset());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("502", response->headers().getStatusValue());

  // Send another request from downstream on the same connection, and make sure
  // a new request reaches upstream on its previous connection.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  const auto buf2 =
      Http2Frame::makeHeadersFrameWithStatus("200", Http2Frame::makeClientStreamId(1));
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf2.begin(), buf2.end())));

  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.rx_messaging_error")->value());
  EXPECT_EQ(
      0,
      test_server_->counter("cluster.cluster_0.upstream_cx_destroy_local_with_active_rq")->value());
  // Expect a local reset due to upstream reset before a response.
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0, true),
              HasSubstr("upstream_reset_before_response_started"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0, true), HasSubstr("UPE"));
}

TEST_P(Http2FloodMitigationTest, UpstreamEmptyData) {
  initializeUpstreamFloodTest();

  // Send client request which will send an upstream request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Start the response with a 200 status.
  auto* upstream = fake_upstreams_.front().get();
  Http2Frame buf = Http2Frame::makeHeadersFrameWithStatus("200", Http2Frame::makeClientStreamId(0),
                                                          Http2Frame::HeadersFlags::EndHeaders);
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  // Send empty data frames.
  for (int i = 0; i < 2; i++) {
    buf = Http2Frame::makeEmptyDataFrame(Http2Frame::makeClientStreamId(0));
    ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));
  }

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForReset());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1,
            test_server_->counter("cluster.cluster_0.http2.inbound_empty_frames_flood")->value());
}

TEST_P(Http2FloodMitigationTest, UpstreamEmptyHeadersContinuation) {
  initializeUpstreamFloodTest();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  auto* upstream = fake_upstreams_.front().get();
  Http2Frame buf = Http2Frame::makeEmptyHeadersFrame(Http2Frame::makeClientStreamId(0));
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  for (int i = 0; i < 2; i++) {
    buf = Http2Frame::makeEmptyContinuationFrame(Http2Frame::makeClientStreamId(0));
    ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));
  }

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_EQ(1,
            test_server_->counter("cluster.cluster_0.http2.inbound_empty_frames_flood")->value());
}

TEST_P(Http2FloodMitigationTest, UpstreamPriorityNoOpenStreams) {
  initializeUpstreamFloodTest();
  floodClient(Http2Frame::makePriorityFrame(Http2Frame::makeClientStreamId(1),
                                            Http2Frame::makeClientStreamId(2)),
              Http2::Utility::OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM * 2 + 1,
              "cluster.cluster_0.http2.inbound_priority_frames_flood");
}

TEST_P(Http2FloodMitigationTest, UpstreamPriorityOneOpenStream) {
  initializeUpstreamFloodTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send response header, but keep the stream open. This should bump the number of OPEN streams
  // tracked by the upstream protocol constraints checker to 1.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  auto buf = serializeFrames(
      Http2Frame::makePriorityFrame(Http2Frame::makeClientStreamId(0),
                                    Http2Frame::makeClientStreamId(1)),
      Http2::Utility::OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM * 2 + 1);

  auto* upstream = fake_upstreams_.front().get();
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  // Upstream connection should be disconnected
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  // Downstream client should get stream reset since upstream sent headers but did not complete the
  // stream
  ASSERT_TRUE(response->waitForReset());
  EXPECT_EQ(
      1, test_server_->counter("cluster.cluster_0.http2.inbound_priority_frames_flood")->value());
}

// Verify that protocol constraint tracker correctly applies limits to the CLOSED streams as well.
TEST_P(Http2FloodMitigationTest, UpstreamPriorityOneClosedStream) {
  initializeUpstreamFloodTest();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send response header and end the stream
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  auto frame = Http2Frame::makePriorityFrame(Http2Frame::makeClientStreamId(0),
                                             Http2Frame::makeClientStreamId(1));
  auto buf = serializeFrames(
      frame, Http2::Utility::OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM * 2 + 1);

  auto* upstream = fake_upstreams_.front().get();
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  // Upstream connection should be disconnected
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  EXPECT_EQ(
      1, test_server_->counter("cluster.cluster_0.http2.inbound_priority_frames_flood")->value());
}

// Verify that the server can detect flooding by the RST_STREAM on stream idle timeout
// after sending response headers.
TEST_P(Http2FloodMitigationTest, UpstreamRstStreamOnStreamIdleTimeout) {
  // Set large buffer limits so the test is not affected by the flow control.
  config_helper_.setBufferLimits(1024 * 1024 * 1024, 1024 * 1024 * 1024);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* stream_idle_timeout = hcm.mutable_stream_idle_timeout();
        std::chrono::milliseconds timeout(1000);
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        stream_idle_timeout->set_seconds(seconds.count());
      });
  initializeUpstreamFloodTest();
  // pre-fill upstream connection 1 away from overflow
  auto response = prefillOutboundUpstreamQueue(ControlFrameFloodLimit);

  // Stream timeout should send 408 downstream and RST_STREAM upstream.
  // Verify that when RST_STREAM overflows upstream queue it is handled correctly
  // by causing upstream connection to be disconnected.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForReset());
  //  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.outbound_control_flood")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_idle_timeout")->value());
}

// Verify that the server can detect flooding by the RST_STREAM sent to upstream when downstream
// disconnects.
TEST_P(Http2FloodMitigationTest, UpstreamRstStreamOnDownstreamRemoteClose) {
  initializeUpstreamFloodTest();
  // pre-fill 1 away from overflow
  auto response = prefillOutboundUpstreamQueue(ControlFrameFloodLimit);

  // Disconnect downstream connection. Envoy should send RST_STREAM to upstream which should
  // overflow the queue and cause the entire upstream connection to be disconnected.
  codec_client_->close();

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.outbound_control_flood")->value());
}

// Verify that the server can detect flood of request METADATA frames
// TODO(#26088): re-enable once the test is flaky no longer
TEST_P(Http2FloodMitigationTest, DISABLED_RequestMetadata) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->set_allow_metadata(true);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  initializeUpstreamFloodTest();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Wait for HEADERS to show up at the upstream server.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Make Envoy's writes into the upstream connection to return EAGAIN, preventing proxying of the
  // METADATA frames
  write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());

  write_matcher_->setWriteReturnsEgain();

  // Send AllFrameFloodLimit + 1 number of METADATA frames from the downstream client to trigger the
  // outbound upstream flood when they are proxied.
  Http::MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
      {"header_key2", "header_value2"},
  };
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::make_unique<Http::MetadataMap>(metadata_map));
  for (uint32_t frame = 0; frame < AllFrameFloodLimit + 1; ++frame) {
    request_encoder_->encodeMetadata(metadata_map_vector);
  }
  codec_client_->connection()->dispatcher().run(Event::Dispatcher::RunType::NonBlock);

  // Upstream connection should be disconnected
  // Downstream client should receive 503 since upstream did not send response headers yet
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  // Verify that the flood check was triggered.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.outbound_flood")->value());
}

// Validate that the default configuration has flood protection enabled.
TEST_P(Http2FloodMitigationTest, UpstreamFloodDetectionIsOnByDefault) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);
  initialize();

  floodClient(Http2Frame::makePingFrame(),
              Http2::Utility::OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES + 1,
              "cluster.cluster_0.http2.outbound_control_flood");
}

TEST_P(Http2FloodMitigationTest, UpstreamRstStreamStormOnDownstreamCloseRegressionTest) {
  const uint32_t num_requests = 80;

  envoy::config::core::v3::Http2ProtocolOptions config;
  config.mutable_max_concurrent_streams()->set_value(num_requests / 2);
  mergeOptions(config);
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  config_helper_.setUpstreamOutboundFramesLimits(AllFrameFloodLimit, ControlFrameFloodLimit);
  beginSession();

  // Send a normal request and wait for the response as a way to prime the upstream connection and
  // ensure that SETTINGS are exchanged. Skipping this step may result in the upstream seeing too
  // many active streams at the same  time and terminating the connection to the proxy since stream
  // limits were not obeyed.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Open a large number of streams and wait until they are active at the proxy.
  for (uint32_t i = 0; i < num_requests; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "host", "/",
                                           {Http2Frame::Header("no_end_stream", "1")});
    sendFrame(request);
  }
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", num_requests,
                               TestUtility::DefaultTimeout);

  // Disconnect downstream connection. Envoy should send RST_STREAM to cancel active upstream
  // requests.
  tcp_client_->close();

  // Wait until the disconnect is detected and all upstream connections have been closed.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0,
                               TestUtility::DefaultTimeout);

  // The disconnect shouldn't trigger an outbound control frame flood.
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.http2.outbound_control_flood")->value());
  // Verify that the upstream connections are still active.
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());
}

// This test validates fix for memory leak in nghttp2 that occurs when
// nghttp2 processes RST_STREAM and GO_AWAY in the same I/O operation.
// Max concurrent request should be limited to 1.
// nghttp2 leaks HEADERS frame and bookkeeping structure.
// The failure occurs in the ASAN build only due to leak checker.
#ifndef WIN32
// Windows does not support override of the socket connect() call
TEST_P(Http2FloodMitigationTest, GoAwayAfterRequestReset) {
  if (std::get<1>(GetParam()) != Http2Impl::Nghttp2) {
    return;
  }
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);
  // Limit concurrent requests to the upstream to 1
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options =
        protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    http_protocol_options->mutable_max_concurrent_streams()->set_value(1);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response_1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // After we've got the first request at the upstream, make the connect() call return EAGAIN (and
  // never connect). This will make the second request to hang in Envoy waiting for either the first
  // connection to free up capacity (i.e. finish with the first request) or the second connection to
  // connect, which will never happen.
  auto* upstream = fake_upstreams_.front().get();
  write_matcher_->setDestinationPort(upstream->localAddress()->ip()->port());
  write_matcher_->setConnectBlock(true);

  auto response_2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The RST_STREAM will cause the capacity to free up on the first connection and the second
  // request to be scheduled on the first connection.
  // The GOAWAY frame will cause nghttp2 to not send any more requests. Without the fix this
  // leaks scheduled request and it is detected by ASAN run.
  const auto rst_stream = Http2Frame::makeResetStreamFrame(Http2Frame::makeClientStreamId(0),
                                                           Http2Frame::ErrorCode::NoError);
  const auto go_away = Http2Frame::makeEmptyGoAwayFrame(0, Http2Frame::ErrorCode::NoError);
  std::string buf(static_cast<std::string>(rst_stream));
  buf.append(static_cast<std::string>(go_away));
  ASSERT_TRUE(upstream->rawWriteConnection(0, buf));

  ASSERT_TRUE(response_1->waitForEndStream());
  ASSERT_TRUE(response_2->waitForEndStream());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  EXPECT_EQ("503", response_1->headers().getStatusValue());
  EXPECT_EQ("503", response_2->headers().getStatusValue());
}
#endif

} // namespace Envoy
