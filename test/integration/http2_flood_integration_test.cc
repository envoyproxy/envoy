#include <algorithm>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/random_generator.h"
#include "common/http/header_map_impl.h"
#include "common/network/socket_option_impl.h"

#include "test/config/utility.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/filters/test_socket_interface.h"
#include "test/integration/http_integration.h"
#include "test/integration/tracked_watermark_buffer.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::HasSubstr;

namespace Envoy {

class SocketInterfaceSwap {
public:
  // Object of this class hold the state determining the IoHandle which
  // should return EAGAIN from the `writev` call.
  struct IoHandleMatcher {
    bool shouldReturnEgain(Envoy::Network::TestIoSocketHandle* io_handle) {
      absl::MutexLock lock(&mutex_);
      if (writev_returns_egain_ && (io_handle->localAddress()->ip()->port() == src_port_ ||
                                    io_handle->peerAddress()->ip()->port() == dst_port_)) {
        ASSERT(matched_iohandle_ == nullptr || matched_iohandle_ == io_handle,
               "Matched multiple io_handles, expected at most one to match.");
        matched_iohandle_ = io_handle;
        return true;
      }
      return false;
    }

    // Source port to match. The port specified should be associated with a listener.
    void setSourcePort(uint32_t port) {
      absl::WriterMutexLock lock(&mutex_);
      src_port_ = port;
      dst_port_ = 0;
    }

    // Destination port to match. The port specified should be associated with a listener.
    void setDestinationPort(uint32_t port) {
      absl::WriterMutexLock lock(&mutex_);
      src_port_ = 0;
      dst_port_ = port;
    }

    void setWritevReturnsEgain() {
      absl::WriterMutexLock lock(&mutex_);
      ASSERT(src_port_ != 0 || dst_port_ != 0);
      writev_returns_egain_ = true;
    }

    void setResumeWrites() {
      absl::MutexLock lock(&mutex_);
      mutex_.Await(absl::Condition(
          +[](Network::TestIoSocketHandle** matched_iohandle) {
            return *matched_iohandle != nullptr;
          },
          &matched_iohandle_));
      writev_returns_egain_ = false;
      matched_iohandle_->activateInDispatcherThreadForTest(Event::FileReadyType::Write);
    }

  private:
    mutable absl::Mutex mutex_;
    uint32_t src_port_ ABSL_GUARDED_BY(mutex_) = 0;
    uint32_t dst_port_ ABSL_GUARDED_BY(mutex_) = 0;
    bool writev_returns_egain_ ABSL_GUARDED_BY(mutex_) = false;
    Network::TestIoSocketHandle* matched_iohandle_{};
  };

  SocketInterfaceSwap() {
    Envoy::Network::SocketInterfaceSingleton::clear();
    test_socket_interface_loader_ = std::make_unique<Envoy::Network::SocketInterfaceLoader>(
        std::make_unique<Envoy::Network::TestSocketInterface>(
            [writev_matcher = writev_matcher_](
                Envoy::Network::TestIoSocketHandle* io_handle, const Buffer::RawSlice*,
                uint64_t) -> absl::optional<Api::IoCallUint64Result> {
              if (writev_matcher->shouldReturnEgain(io_handle)) {
                return Api::IoCallUint64Result(
                    0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                                       Network::IoSocketError::deleteIoError));
              }
              return absl::nullopt;
            }));
  }

  ~SocketInterfaceSwap() {
    test_socket_interface_loader_.reset();
    Envoy::Network::SocketInterfaceSingleton::initialize(previous_socket_interface_);
  }

protected:
  Envoy::Network::SocketInterface* const previous_socket_interface_{
      Envoy::Network::SocketInterfaceSingleton::getExisting()};
  std::shared_ptr<IoHandleMatcher> writev_matcher_{std::make_shared<IoHandleMatcher>()};
  std::unique_ptr<Envoy::Network::SocketInterfaceLoader> test_socket_interface_loader_;
};

// It is important that the new socket interface is installed before any I/O activity starts and
// the previous one is restored after all I/O activity stops. Since the HttpIntegrationTest
// destructor stops Envoy the SocketInterfaceSwap destructor needs to run after it. This order of
// multiple inheritance ensures that SocketInterfaceSwap destructor runs after
// Http2FrameIntegrationTest destructor completes.
class Http2FloodMitigationTest
    : public SocketInterfaceSwap,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public Http2RawFrameIntegrationTest {
public:
  Http2FloodMitigationTest() : Http2RawFrameIntegrationTest(std::get<0>(GetParam())) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_h2_watermark_improvements",
                                      http2FlowControlV2() ? "true" : "false");

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(1); });

    setServerBufferFactory(buffer_factory_);
  }

  bool http2FlowControlV2() const { return std::get<1>(GetParam()); }

protected:
  bool initializeUpstreamFloodTest();
  std::vector<char> serializeFrames(const Http2Frame& frame, uint32_t num_frames);
  void floodServer(const Http2Frame& frame, const std::string& flood_stat, uint32_t num_frames);
  void floodServer(absl::string_view host, absl::string_view path,
                   Http2Frame::ResponseStatus expected_http_status, const std::string& flood_stat,
                   uint32_t num_frames);
  void floodClient(const Http2Frame& frame, uint32_t num_frames, const std::string& flood_stat);

  void setNetworkConnectionBufferSize();
  void initialize() override {
    Http2RawFrameIntegrationTest::initialize();
    writev_matcher_->setSourcePort(lookupPort("http"));
  }
  void prefillOutboundDownstreamQueue(uint32_t data_frame_count, uint32_t data_frame_size = 10);
  IntegrationStreamDecoderPtr prefillOutboundUpstreamQueue(uint32_t frame_count);
  void triggerListenerDrain();

  std::shared_ptr<Buffer::TrackedWatermarkBufferFactory> buffer_factory_ =
      std::make_shared<Buffer::TrackedWatermarkBufferFactory>();
};

static std::string http2FloodMitigationTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
  return absl::StrCat(std::get<0>(params.param) == Network::Address::IpVersion::v4 ? "IPv4"
                                                                                   : "IPv6",
                      "_", std::get<1>(params.param) ? "Http2FlowControlV2" : "Http2FlowControlV1");
}

INSTANTIATE_TEST_SUITE_P(
    IpVersions, Http2FloodMitigationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    http2FloodMitigationTestParamsToString);

bool Http2FloodMitigationTest::initializeUpstreamFloodTest() {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.upstream_http2_flood_checks",
                                    "true");
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  // set lower upstream outbound frame limits to make tests run faster
  config_helper_.setUpstreamOutboundFramesLimits(AllFrameFloodLimit, ControlFrameFloodLimit);
  initialize();
  return true;
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

std::vector<char> Http2FloodMitigationTest::serializeFrames(const Http2Frame& frame,
                                                            uint32_t num_frames) {
  // make sure all frames can fit into 16k buffer
  ASSERT(num_frames <= ((16u * 1024u) / frame.size()));
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
  test_server_->waitForCounterGe("http.config_test.downstream_cx_delayed_close_timeout", 1,
                                 TestUtility::DefaultTimeout);
}

// Send header only request, flood client, and verify that the upstream is disconnected and client
// receives 503.
void Http2FloodMitigationTest::floodClient(const Http2Frame& frame, uint32_t num_frames,
                                           const std::string& flood_stat) {
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Make Envoy's writes into the upstream connection to return EAGAIN
  writev_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());

  auto buf = serializeFrames(frame, num_frames);

  writev_matcher_->setWritevReturnsEgain();
  auto* upstream = fake_upstreams_.front().get();
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  // Upstream connection should be disconnected
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  // Downstream client should receive 503 since upstream did not send response headers yet
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().getStatusValue());
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
  writev_matcher_->setWritevReturnsEgain();
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
  // such the the next response triggers flood protection.
  // Simulate TCP push back on the Envoy's downstream network socket, so that outbound frames
  // start to accumulate in the transport socket buffer.
  writev_matcher_->setWritevReturnsEgain();

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
  writev_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());

  auto buf = serializeFrames(Http2Frame::makePingFrame(), frame_count);

  writev_matcher_->setWritevReturnsEgain();
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
  writev_matcher_->setWritevReturnsEgain();
  floodServer(Http2Frame::makePingFrame(), "http2.outbound_control_flood",
              ControlFrameFloodLimit + 1);
}

TEST_P(Http2FloodMitigationTest, Settings) {
  setNetworkConnectionBufferSize();
  beginSession();
  writev_matcher_->setWritevReturnsEgain();
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
  writev_matcher_->setWritevReturnsEgain();

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
  EXPECT_EQ(8, buffer_factory_->numBuffersCreated());

  // Expect at least 1000 1 byte data frames in the output buffer. Each data frame comes with a
  // 9-byte frame header; 10 bytes per data frame, 10000 bytes total. The output buffer should also
  // contain response headers, which should be less than 100 bytes.
  EXPECT_LE(10000, buffer_factory_->maxBufferSize());
  EXPECT_GE(10100, buffer_factory_->maxBufferSize());

  // The response pipeline input buffer could end up with the full upstream response in 1 go, but
  // there are no guarantees of that being the case.
  EXPECT_LE(10000, buffer_factory_->sumMaxBufferSizes());
  // The max size of the input and output buffers used in the response pipeline is around 10kb each.
  EXPECT_GE(22000, buffer_factory_->sumMaxBufferSizes());
  // Verify that all buffers have watermarks set.
  EXPECT_THAT(buffer_factory_->highWatermarkRange(),
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
TEST_P(Http2FloodMitigationTest, Envoy100ContinueHeaders) {
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
  writev_matcher_->setWritevReturnsEgain();

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
  config_helper_.addFilter(R"EOF(
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

  writev_matcher_->setWritevReturnsEgain();

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
  writev_matcher_->setWritevReturnsEgain();

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
TEST_P(Http2FloodMitigationTest, KeepAliveTimeeTriggersFloodProtection) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* keep_alive = hcm.mutable_http2_protocol_options()->mutable_connection_keepalive();
        keep_alive->mutable_interval()->set_nanos(500 * 1000 * 1000);
        keep_alive->mutable_timeout()->set_seconds(1);
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
  writev_matcher_->setSourcePort(fake_upstreams_[0]->localAddress()->ip()->port());

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

  // Since we do not send any DATA frames, only 4 sequential WINDOW_UPDATE frames should
  // trigger flood protection.
  floodServer(Http2Frame::makeWindowUpdateFrame(request_stream_id, 1),
              "http2.inbound_window_update_frames_flood", 4);
}

// Verify that the HTTP/2 connection is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, ZerolenHeader) {
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
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("http2.invalid.header.field"));
  // expect a downstream protocol error.
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("DPE"));
}

// Verify that only the offending stream is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, ZerolenHeaderAllowed) {
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
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("http2.invalid.header.field"));
  // expect Downstream Protocol Error
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("DPE"));
}

TEST_P(Http2FloodMitigationTest, UpstreamPingFlood) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }

  floodClient(Http2Frame::makePingFrame(), ControlFrameFloodLimit + 1,
              "cluster.cluster_0.http2.outbound_control_flood");
}

TEST_P(Http2FloodMitigationTest, UpstreamSettings) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }

  floodClient(Http2Frame::makeEmptySettingsFrame(), ControlFrameFloodLimit + 1,
              "cluster.cluster_0.http2.outbound_control_flood");
}

TEST_P(Http2FloodMitigationTest, UpstreamWindowUpdate) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }

  floodClient(Http2Frame::makeWindowUpdateFrame(0, 1), 4,
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
  if (!initializeUpstreamFloodTest()) {
    return;
  }

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  auto* upstream = fake_upstreams_.front().get();
  auto buf = Http2Frame::makeEmptyHeadersFrame(Http2Frame::makeClientStreamId(0),
                                               Http2Frame::HeadersFlags::None);
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(1,
            test_server_->counter("cluster.cluster_0.http2.inbound_empty_frames_flood")->value());
}

// Verify that the HTTP/2 connection is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, UpstreamZerolenHeader) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }

  // Send client request which will send an upstream request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send an upstream reply.
  auto* upstream = fake_upstreams_.front().get();
  const auto buf =
      Http2Frame::makeMalformedResponseWithZerolenHeader(Http2Frame::makeClientStreamId(0));
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  response->waitForEndStream();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.rx_messaging_error")->value());
  EXPECT_EQ(
      1,
      test_server_->counter("cluster.cluster_0.upstream_cx_destroy_local_with_active_rq")->value());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Verify that the HTTP/2 connection is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, UpstreamZerolenHeaderAllowed) {
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
  if (!initializeUpstreamFloodTest()) {
    return;
  }

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
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().getStatusValue());

  // Send another request from downstream on the same connection, and make sure
  // a new request reaches upstream on its previous connection.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  const auto buf2 =
      Http2Frame::makeHeadersFrameWithStatus("200", Http2Frame::makeClientStreamId(1));
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf2.begin(), buf2.end())));

  response2->waitForEndStream();
  EXPECT_EQ("200", response2->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.rx_messaging_error")->value());
  EXPECT_EQ(
      0,
      test_server_->counter("cluster.cluster_0.upstream_cx_destroy_local_with_active_rq")->value());
  // Expect a local reset due to upstream reset before a response.
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              HasSubstr("upstream_reset_before_response_started"));
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("LR"));
}

TEST_P(Http2FloodMitigationTest, UpstreamEmptyData) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }

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
  response->waitForReset();
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1,
            test_server_->counter("cluster.cluster_0.http2.inbound_empty_frames_flood")->value());
}

TEST_P(Http2FloodMitigationTest, UpstreamEmptyHeadersContinuation) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }

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
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(1,
            test_server_->counter("cluster.cluster_0.http2.inbound_empty_frames_flood")->value());
}

TEST_P(Http2FloodMitigationTest, UpstreamPriorityNoOpenStreams) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }

  // TODO(yanavlasov): The protocol constraint tracker for upstream connections considers the stream
  // to be in the OPEN state after the server sends complete response headers. The correctness of
  // this is debatable and needs to be revisited.

  // The `floodClient` method sends request headers to open upstream connection, but upstream does
  // not send any response. In this case the number of streams tracked by the upstream protocol
  // constraints checker is still 0.
  floodClient(Http2Frame::makePriorityFrame(Http2Frame::makeClientStreamId(1),
                                            Http2Frame::makeClientStreamId(2)),
              Http2::Utility::OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM + 1,
              "cluster.cluster_0.http2.inbound_priority_frames_flood");
}

TEST_P(Http2FloodMitigationTest, UpstreamPriorityOneOpenStream) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }
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
  response->waitForReset();
  EXPECT_EQ(
      1, test_server_->counter("cluster.cluster_0.http2.inbound_priority_frames_flood")->value());
}

// Verify that protocol constraint tracker correctly applies limits to the CLOSED streams as well.
TEST_P(Http2FloodMitigationTest, UpstreamPriorityOneClosedStream) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send response header and end the stream
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForEndStream();
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
  if (!initializeUpstreamFloodTest()) {
    return;
  }
  // pre-fill upstream connection 1 away from overflow
  auto response = prefillOutboundUpstreamQueue(ControlFrameFloodLimit);

  // Stream timeout should send 408 downstream and RST_STREAM upstream.
  // Verify that when RST_STREAM overflows upstream queue it is handled correctly
  // by causing upstream connection to be disconnected.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForReset();
  //  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.outbound_control_flood")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_idle_timeout")->value());
}

// Verify that the server can detect flooding by the RST_STREAM sent to upstream when downstream
// disconnects.
TEST_P(Http2FloodMitigationTest, UpstreamRstStreamOnDownstreamRemoteClose) {
  if (!initializeUpstreamFloodTest()) {
    return;
  }
  // pre-fill 1 away from overflow
  auto response = prefillOutboundUpstreamQueue(ControlFrameFloodLimit);

  // Disconnect downstream connection. Envoy should send RST_STREAM to upstream which should
  // overflow the queue and cause the entire upstream connection to be disconnected.
  codec_client_->close();

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.outbound_control_flood")->value());
}

// Verify that the server can detect flood of request METADATA frames
TEST_P(Http2FloodMitigationTest, RequestMetadata) {
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

  if (!initializeUpstreamFloodTest()) {
    return;
  }

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
  writev_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());

  writev_matcher_->setWritevReturnsEgain();

  // Send AllFrameFloodLimit + 1 number of METADATA frames from the downstream client to trigger the
  // outbound upstream flood when they are proxied.
  Http::MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
      {"header_key2", "header_value2"},
  };
  for (uint32_t frame = 0; frame < AllFrameFloodLimit + 1; ++frame) {
    if (response->reset()) {
      // Stream was reset, codec_client_ no longer valid.
      break;
    }
    codec_client_->sendMetadata(*request_encoder_, metadata_map);
  }

  // Upstream connection should be disconnected
  // Downstream client should receive 503 since upstream did not send response headers yet
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().getStatusValue());
  // Verify that the flood check was triggered.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.outbound_flood")->value());
}

class Http2BufferWatermarksTest : public Http2FloodMitigationTest {
public:
  struct BufferParams {
    uint32_t connection_watermark;
    uint32_t downstream_h2_stream_window;
    uint32_t downstream_h2_conn_window;
    uint32_t upstream_h2_stream_window;
    uint32_t upstream_h2_conn_window;
  };

  void initializeWithBufferConfig(
      const BufferParams& buffer_params, uint32_t num_responses,
      bool enable_downstream_frame_limits,
      FakeHttpConnection::Type upstream_protocol = FakeHttpConnection::Type::HTTP2) {
    config_helper_.setBufferLimits(buffer_params.connection_watermark,
                                   buffer_params.connection_watermark);

    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* h2_options = hcm.mutable_http2_protocol_options();
          h2_options->mutable_max_concurrent_streams()->set_value(num_responses);
          h2_options->mutable_initial_stream_window_size()->set_value(
              buffer_params.downstream_h2_stream_window);
          h2_options->mutable_initial_connection_window_size()->set_value(
              buffer_params.downstream_h2_conn_window);
        });

    if (upstream_protocol == FakeHttpConnection::Type::HTTP2) {
      config_helper_.addConfigModifier(
          [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
            ConfigHelper::HttpProtocolOptions protocol_options;
            auto* upstream_h2_options =
                protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
            upstream_h2_options->mutable_max_concurrent_streams()->set_value(100);
            upstream_h2_options->mutable_initial_stream_window_size()->set_value(
                buffer_params.upstream_h2_stream_window);
            upstream_h2_options->mutable_initial_connection_window_size()->set_value(
                buffer_params.upstream_h2_conn_window);
            for (auto& cluster_config : *bootstrap.mutable_static_resources()->mutable_clusters()) {
              ConfigHelper::setProtocolOptions(cluster_config, protocol_options);
            }
          });
    }

    autonomous_upstream_ = true;
    autonomous_allow_incomplete_streams_ = true;
    if (enable_downstream_frame_limits) {
      beginSession(upstream_protocol);
    } else {
      beginSession(upstream_protocol, std::numeric_limits<uint32_t>::max(),
                   std::numeric_limits<uint32_t>::max());
    }
  }

  std::vector<uint32_t> sendRequests(uint32_t num_responses, uint32_t chunks_per_response,
                                     uint32_t chunk_size,
                                     uint32_t min_buffered_bytes_per_stream = 0) {
    uint32_t expected_response_size = chunks_per_response * chunk_size;
    std::vector<uint32_t> stream_ids;

    auto connection_window_update = Http2Frame::makeWindowUpdateFrame(0, 1024 * 1024 * 1024);
    sendFrame(connection_window_update);

    for (uint32_t idx = 0; idx < num_responses; ++idx) {
      const uint32_t request_stream_id = Http2Frame::makeClientStreamId(idx);
      stream_ids.push_back(request_stream_id);
      const auto request = Http2Frame::makeRequest(
          request_stream_id, "host", "/test/long/url",
          {Http2Frame::Header("response_data_blocks", absl::StrCat(chunks_per_response)),
           Http2Frame::Header("response_size_bytes", absl::StrCat(chunk_size)),
           Http2Frame::Header("no_trailers", "0")});
      EXPECT_EQ(request_stream_id, request.streamId());
      sendFrame(request);

      auto stream_window_update =
          Http2Frame::makeWindowUpdateFrame(request_stream_id, expected_response_size);
      sendFrame(stream_window_update);

      if (min_buffered_bytes_per_stream > 0) {
        EXPECT_TRUE(buffer_factory_->waitUntilTotalBufferedExceeds(
            (idx + 1) * min_buffered_bytes_per_stream, TestUtility::DefaultTimeout))
            << "idx: " << idx << " buffer total: " << buffer_factory_->totalBufferSize()
            << " buffer max: " << buffer_factory_->maxBufferSize();
      }
    }

    return stream_ids;
  }

  void runTestWithBufferConfig(
      const BufferParams& buffer_params, uint32_t num_responses, uint32_t chunks_per_response,
      uint32_t chunk_size, uint32_t min_buffered_bytes, uint32_t min_buffered_bytes_per_stream = 0,
      FakeHttpConnection::Type upstream_protocol = FakeHttpConnection::Type::HTTP2) {
    uint32_t expected_response_size = chunks_per_response * chunk_size;

    initializeWithBufferConfig(buffer_params, num_responses, false, upstream_protocol);

    // Simulate TCP push back on the Envoy's downstream network socket, so that outbound frames
    // start to accumulate in the transport socket buffer.
    writev_matcher_->setWritevReturnsEgain();

    std::vector<uint32_t> stream_ids =
        sendRequests(num_responses, chunks_per_response, chunk_size, min_buffered_bytes_per_stream);

    ASSERT_TRUE(buffer_factory_->waitUntilTotalBufferedExceeds(min_buffered_bytes,
                                                               TestUtility::DefaultTimeout))
        << "buffer total: " << buffer_factory_->totalBufferSize()
        << " buffer max: " << buffer_factory_->maxBufferSize();
    writev_matcher_->setResumeWrites();

    std::map<uint32_t, ResponseInfo> response_info;
    parseResponse(&response_info, num_responses);
    ASSERT_EQ(stream_ids.size(), response_info.size());
    for (uint32_t stream_id : stream_ids) {
      EXPECT_EQ(expected_response_size, response_info[stream_id].response_size_) << stream_id;
    }

    tcp_client_->close();
  }

  struct ResponseInfo {
    uint32_t response_size_ = 0;
    bool reset_stream_ = false;
    bool end_stream_ = false;
  };

  void parseResponse(std::map<uint32_t, ResponseInfo>* response_info, uint32_t expect_completed) {
    parseResponseUntil(response_info, [&]() -> bool {
      uint32_t completed = 0;
      for (const auto& item : *response_info) {
        if (item.second.end_stream_ || item.second.reset_stream_) {
          ++completed;
        }
      }
      return completed >= expect_completed;
    });
  }

  void parseResponseUntil(std::map<uint32_t, ResponseInfo>* response_info,
                          const std::function<bool()>& stop_predicate) {
    while (!stop_predicate()) {
      auto frame = readFrame();
      switch (frame.type()) {
      case Http2Frame::Type::Headers: {
        uint32_t stream_id = frame.streamId();
        auto result = response_info->emplace(stream_id, ResponseInfo());
        ASSERT(result.second);
      } break;
      case Http2Frame::Type::Data: {
        uint32_t stream_id = frame.streamId();
        auto it = response_info->find(stream_id);
        ASSERT(it != response_info->end());
        it->second.response_size_ += frame.payloadSize();
        if (frame.flags() > 0) {
          it->second.end_stream_ = true;
        }
      } break;
      case Http2Frame::Type::RstStream: {
        uint32_t stream_id = frame.streamId();
        ENVOY_LOG_MISC(critical, "rst {}", stream_id);
        auto it = response_info->find(stream_id);
        ASSERT(it != response_info->end());
        it->second.reset_stream_ = true;
      } break;
      default:
        RELEASE_ASSERT(false, absl::StrCat("Unknown frame type: ", frame.type()));
      }
    }
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, Http2BufferWatermarksTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    http2FloodMitigationTestParamsToString);

// Verify buffering behavior when the downstream and upstream H2 stream high watermarks are
// configured to the same value.
TEST_P(Http2BufferWatermarksTest, DataFlowControlSymmetricStreamConfig) {
  int num_requests = 5;
  uint32_t connection_watermark = 32768;
  uint32_t h2_stream_window = 128 * 1024;
  uint32_t h2_conn_window = 1024 * 1024 * 1024; // Effectively unlimited
  uint64_t kBlockSize = 1024;
  uint64_t kNumBlocks = 5 * h2_stream_window / kBlockSize;

  runTestWithBufferConfig(
      {connection_watermark, h2_stream_window, h2_conn_window, h2_stream_window, h2_conn_window},
      num_requests, kNumBlocks, kBlockSize, h2_stream_window * num_requests, h2_stream_window);

  // Verify that the flood check was not triggered
  EXPECT_EQ(0, test_server_->counter("http2.outbound_flood")->value());

  EXPECT_EQ(24, buffer_factory_->numBuffersCreated());
  if (http2FlowControlV2()) {
    EXPECT_LE(h2_stream_window - connection_watermark, buffer_factory_->maxBufferSize());
    EXPECT_GE(3 * h2_stream_window, buffer_factory_->maxBufferSize());
    auto overflow_info = buffer_factory_->maxOverflowRatio();
    EXPECT_LE(1.0, std::get<0>(overflow_info));
    EXPECT_GE(3.0, std::get<0>(overflow_info));
    // Max overflow happens on the stream buffer.
    EXPECT_EQ(h2_stream_window, std::get<1>(overflow_info));
  } else {
    EXPECT_LE(h2_stream_window * num_requests / 2, buffer_factory_->maxBufferSize());
    EXPECT_GE(h2_stream_window * num_requests + 2 * connection_watermark,
              buffer_factory_->maxBufferSize());
    auto overflow_info = buffer_factory_->maxOverflowRatio();
    EXPECT_LT(18.0, std::get<0>(overflow_info));
    // Max overflow happens on the connection buffer.
    EXPECT_EQ(connection_watermark, std::get<1>(overflow_info));
  }
  EXPECT_LT(h2_stream_window * num_requests, buffer_factory_->sumMaxBufferSizes());
  EXPECT_THAT(buffer_factory_->highWatermarkRange(), testing::Pair(32768, 128 * 1024));
}

// Verify buffering behavior when the upstream buffer high watermarks are configured to a larger
// value than downstream.
TEST_P(Http2BufferWatermarksTest, DataFlowControlLargeUpstreamStreamWindow) {
  int num_requests = 5;
  uint32_t connection_watermark = 32768;
  uint32_t downstream_h2_stream_window = 64 * 1024;
  uint32_t downstream_h2_conn_window = 256 * 1024;
  uint32_t upstream_h2_stream_window = 512 * 1024;
  uint32_t upstream_h2_conn_window = 1024 * 1024 * 1024; // Effectively unlimited
  uint64_t kBlockSize = 1024;
  uint64_t kNumBlocks = 5 * upstream_h2_stream_window / kBlockSize;

  runTestWithBufferConfig(
      {connection_watermark, downstream_h2_stream_window, downstream_h2_conn_window,
       upstream_h2_stream_window, upstream_h2_conn_window},
      num_requests, kNumBlocks, kBlockSize, upstream_h2_stream_window * num_requests);

  // Verify that the flood check was not triggered
  EXPECT_EQ(0, test_server_->counter("http2.outbound_flood")->value());

  EXPECT_EQ(24, buffer_factory_->numBuffersCreated());
  if (http2FlowControlV2()) {
    EXPECT_LE(upstream_h2_stream_window - connection_watermark, buffer_factory_->maxBufferSize());
    EXPECT_GE(2 * upstream_h2_stream_window, buffer_factory_->maxBufferSize());
    auto overflow_info = buffer_factory_->maxOverflowRatio();
    EXPECT_LE(7.5, std::get<0>(overflow_info));
    EXPECT_GT(10.0, std::get<0>(overflow_info));
    // Max overflow happens on the downstream H2 stream buffer.
    EXPECT_EQ(downstream_h2_stream_window, std::get<1>(overflow_info));
  } else {
    EXPECT_LE(upstream_h2_stream_window * num_requests - connection_watermark,
              buffer_factory_->maxBufferSize());
    EXPECT_GE(upstream_h2_stream_window * num_requests + 2 * connection_watermark,
              buffer_factory_->maxBufferSize());
    auto overflow_info = buffer_factory_->maxOverflowRatio();
    EXPECT_LT(30.0, std::get<0>(overflow_info));
    // Max overflow happens on the connection buffer.
    EXPECT_EQ(connection_watermark, std::get<1>(overflow_info));
  }
  EXPECT_LT(upstream_h2_stream_window * num_requests, buffer_factory_->sumMaxBufferSizes());
  EXPECT_THAT(buffer_factory_->highWatermarkRange(), testing::Pair(32768, 512 * 1024));
}

// Verify buffering behavior when the downstream buffer high watermarks are configured to a larger
// value than upstream.
TEST_P(Http2BufferWatermarksTest, DataFlowControlLargeDownstreamStreamWindow) {
  int num_requests = 5;
  uint32_t connection_watermark = 32768;
  uint32_t downstream_h2_stream_window = 512 * 1024;
  uint32_t downstream_h2_conn_window = 64 * 1024;
  uint32_t upstream_h2_stream_window = 64 * 1024;
  uint32_t upstream_h2_conn_window = 1024 * 1024 * 1024; // Effectively unlimited
  uint64_t kBlockSize = 1024;
  uint64_t kNumBlocks = 5 * downstream_h2_stream_window / kBlockSize;

  runTestWithBufferConfig({connection_watermark, downstream_h2_stream_window,
                           downstream_h2_conn_window, upstream_h2_stream_window,
                           upstream_h2_conn_window},
                          num_requests, kNumBlocks, kBlockSize,
                          http2FlowControlV2() ? downstream_h2_stream_window * num_requests
                                               : upstream_h2_stream_window * num_requests);

  // Verify that the flood check was not triggered
  EXPECT_EQ(0, test_server_->counter("http2.outbound_flood")->value());

  EXPECT_EQ(24, buffer_factory_->numBuffersCreated());
  if (http2FlowControlV2()) {
    EXPECT_LE(downstream_h2_stream_window - connection_watermark, buffer_factory_->maxBufferSize());
    EXPECT_GE(2 * downstream_h2_stream_window, buffer_factory_->maxBufferSize());
    auto overflow_info = buffer_factory_->maxOverflowRatio();
    EXPECT_LE(1.0, std::get<0>(overflow_info));
    EXPECT_GT(2.0, std::get<0>(overflow_info));

    EXPECT_LT(downstream_h2_stream_window * num_requests, buffer_factory_->sumMaxBufferSizes());
    EXPECT_GT(2 * downstream_h2_stream_window * num_requests, buffer_factory_->sumMaxBufferSizes());
  } else {
    EXPECT_LE(num_requests * upstream_h2_stream_window - connection_watermark,
              buffer_factory_->maxBufferSize());
    EXPECT_GE(2 * num_requests * upstream_h2_stream_window, buffer_factory_->maxBufferSize());
    auto overflow_info = buffer_factory_->maxOverflowRatio();
    EXPECT_LT(9.0, std::get<0>(overflow_info));
    // Max overflow happens on the connection buffer.
    EXPECT_EQ(connection_watermark, std::get<1>(overflow_info));

    EXPECT_LT(upstream_h2_stream_window * num_requests, buffer_factory_->sumMaxBufferSizes());
    EXPECT_GT(2 * upstream_h2_stream_window * num_requests, buffer_factory_->sumMaxBufferSizes());
  }
  EXPECT_THAT(buffer_factory_->highWatermarkRange(), testing::Pair(32768, 512 * 1024));
}

// Verify that buffering is limited when using HTTP1 upstreams.
TEST_P(Http2BufferWatermarksTest, DataFlowControlHttp1Upstream) {
  int num_requests = 25;
  uint32_t connection_watermark = 32768;
  uint32_t h2_stream_window = 128 * 1024;
  uint32_t h2_conn_window = 1024 * 1024 * 1024; // Effectively unlimited
  uint64_t kBlockSize = 1024;
  uint64_t kNumBlocks = 5 * h2_stream_window / kBlockSize;

  runTestWithBufferConfig(
      {connection_watermark, h2_stream_window, h2_conn_window, 0, 0}, num_requests, kNumBlocks,
      kBlockSize, http2FlowControlV2() ? h2_stream_window * num_requests : connection_watermark,
      http2FlowControlV2() ? h2_stream_window : 0, FakeHttpConnection::Type::HTTP1);

  // Verify that the flood check was not triggered
  EXPECT_EQ(0, test_server_->counter("http2.outbound_flood")->value());

  EXPECT_EQ(127, buffer_factory_->numBuffersCreated());
  if (http2FlowControlV2()) {
    EXPECT_LE(h2_stream_window - connection_watermark, buffer_factory_->maxBufferSize());
    EXPECT_GE(h2_stream_window + 2 * connection_watermark, buffer_factory_->maxBufferSize());
    auto overflow_info = buffer_factory_->maxOverflowRatio();
    EXPECT_LE(1.0, std::get<0>(overflow_info));
    EXPECT_GE(2.0, std::get<0>(overflow_info));
    EXPECT_EQ(connection_watermark, std::get<1>(overflow_info));
  } else {
    EXPECT_LE(connection_watermark, buffer_factory_->maxBufferSize());
    EXPECT_GE(3 * connection_watermark, buffer_factory_->maxBufferSize());
    auto overflow_info = buffer_factory_->maxOverflowRatio();
    EXPECT_LT(1.0, std::get<0>(overflow_info));
    EXPECT_GE(3.0, std::get<0>(overflow_info));
    // Max overflow happens on the connection buffer.
    EXPECT_EQ(connection_watermark, std::get<1>(overflow_info));
  }
  EXPECT_THAT(buffer_factory_->highWatermarkRange(), testing::Pair(32768, 128 * 1024));
}

// Verify that buffering is limited when handling small responses.
TEST_P(Http2BufferWatermarksTest, SmallResponseBuffering) {
  int num_requests = 200;
  uint64_t kNumBlocks = 1;
  uint64_t kBlockSize = 10240;
  uint32_t connection_watermark = 32768;
  uint32_t downstream_h2_stream_window = 64 * 1024;
  uint32_t downstream_h2_conn_window = 256 * 1024;
  uint32_t upstream_h2_stream_window = 512 * 1024;
  uint32_t upstream_h2_conn_window = 1024 * 1024 * 1024; // Effectively unlimited

  runTestWithBufferConfig(
      {connection_watermark, downstream_h2_stream_window, downstream_h2_conn_window,
       upstream_h2_stream_window, upstream_h2_conn_window},
      num_requests, kNumBlocks, kBlockSize, num_requests * kNumBlocks * kBlockSize, kBlockSize);

  EXPECT_LT(800, buffer_factory_->numBuffersCreated());
  if (http2FlowControlV2()) {
    EXPECT_LE(connection_watermark, buffer_factory_->maxBufferSize());
    EXPECT_GE(2 * connection_watermark, buffer_factory_->maxBufferSize());
    EXPECT_GT(2.0, std::get<0>(buffer_factory_->maxOverflowRatio()));
  } else {
    EXPECT_LE(50 * connection_watermark, buffer_factory_->maxBufferSize());
    EXPECT_GE(70 * connection_watermark, buffer_factory_->maxBufferSize());
    auto overflow_info = buffer_factory_->maxOverflowRatio();
    EXPECT_LT(50.0, std::get<0>(overflow_info));
    // Max overflow happens on the connection buffer.
    EXPECT_EQ(connection_watermark, std::get<1>(overflow_info));
  }
  EXPECT_THAT(buffer_factory_->highWatermarkRange(), testing::Pair(32768, 512 * 1024));
}

// Verify that control frame protections take effect even if control frames end up queued internally
// by nghttp2.
TEST_P(Http2BufferWatermarksTest, PingFloodAfterHighWatermark) {
  int num_requests = 1;
  uint64_t kNumBlocks = 10;
  uint64_t kBlockSize = 16 * 1024;
  uint32_t connection_watermark = 32768;
  uint32_t h2_stream_window = 64 * 1024;
  uint32_t h2_conn_window = 1024 * 1024 * 1024; // Effectively unlimited

  initializeWithBufferConfig(
      {connection_watermark, h2_stream_window, h2_conn_window, h2_stream_window, h2_conn_window},
      num_requests, true);

  writev_matcher_->setWritevReturnsEgain();

  sendRequests(num_requests, kNumBlocks, kBlockSize, connection_watermark + h2_stream_window);

  floodServer(Http2Frame::makePingFrame(), "http2.outbound_control_flood",
              ControlFrameFloodLimit + 1);
}

TEST_P(Http2BufferWatermarksTest, RstStreamWhileBlockedProxyingDataFrame) {
  int num_requests = 5;
  uint64_t kNumBlocks = 20;
  uint64_t kBlockSize = 16 * 1024;
  uint32_t connection_watermark = 32768;
  uint32_t h2_stream_window = 128 * 1024;
  uint32_t h2_conn_window = 1024 * 1024 * 1024; // Effectively unlimited
  uint32_t expected_response_size = kNumBlocks * kBlockSize;

  initializeWithBufferConfig(
      {connection_watermark, h2_stream_window, h2_conn_window, h2_stream_window, h2_conn_window},
      num_requests, true);

  writev_matcher_->setWritevReturnsEgain();

  sendRequests(num_requests, kNumBlocks, kBlockSize, h2_stream_window);
  if (http2FlowControlV2()) {
    test_server_->waitForGaugeEq("http2.streams_active", 5);
  }

  // Reset streams in reverse order except for the most recently created stream. Verify that the rst
  // streams are properly cancelled.
  for (int32_t idx = num_requests - 2; idx >= 0; --idx) {
    const uint32_t request_stream_id = Http2Frame::makeClientStreamId(idx);
    auto rst_frame =
        Http2Frame::makeResetStreamFrame(request_stream_id, Http2Frame::ErrorCode::Cancel);
    sendFrame(rst_frame);
  }

  if (http2FlowControlV2()) {
    test_server_->waitForGaugeEq("http2.streams_active", 1);
  }
  writev_matcher_->setResumeWrites();

  uint32_t first_stream = Http2Frame::makeClientStreamId(0);
  uint32_t last_stream = Http2Frame::makeClientStreamId(num_requests - 1);

  std::map<uint32_t, ResponseInfo> response_info;
  parseResponse(&response_info, 1);
  if (http2FlowControlV2()) {
    ASSERT_EQ(2, response_info.size());
    // Full response on last_stream.
    EXPECT_EQ(expected_response_size, response_info[last_stream].response_size_);
    // Partial response on first_stream.
    EXPECT_LT(0, response_info[first_stream].response_size_);
    EXPECT_GT(expected_response_size, response_info[first_stream].response_size_);
    EXPECT_GT(2 * h2_stream_window, response_info[first_stream].response_size_);
  } else {
    ASSERT_EQ(num_requests, response_info.size());
    for (auto& [stream_id, info] : response_info) {
      if (stream_id == last_stream) {
        EXPECT_EQ(expected_response_size, response_info[stream_id].response_size_) << stream_id;
      } else {
        EXPECT_LT(0, response_info[stream_id].response_size_) << stream_id;
        EXPECT_GT(expected_response_size, response_info[stream_id].response_size_) << stream_id;
        EXPECT_GT(2 * h2_stream_window, response_info[stream_id].response_size_) << stream_id;
      }
    }
  }

  tcp_client_->close();
}

// Create some requests and reset them immediately. Do not wait for the request to reach a specific
// point in the state machine. Only expect a full response on the one stream that we didn't reset.
TEST_P(Http2BufferWatermarksTest, RstStreamQuickly) {
  int num_requests = 5;
  uint64_t kNumBlocks = 20;
  uint64_t kBlockSize = 16 * 1024;
  uint32_t connection_watermark = 32768;
  uint32_t h2_stream_window = 128 * 1024;
  uint32_t h2_conn_window = 1024 * 1024 * 1024; // Effectively unlimited
  uint32_t expected_response_size = kNumBlocks * kBlockSize;

  initializeWithBufferConfig(
      {connection_watermark, h2_stream_window, h2_conn_window, h2_stream_window, h2_conn_window},
      num_requests, true);

  writev_matcher_->setWritevReturnsEgain();

  sendRequests(num_requests, kNumBlocks, kBlockSize);
  // Reset streams in reverse order except for the most recently created stream. Verify that the rst
  // streams are properly cancelled.
  for (int32_t idx = num_requests - 2; idx >= 0; --idx) {
    const uint32_t request_stream_id = Http2Frame::makeClientStreamId(idx);
    auto rst_frame =
        Http2Frame::makeResetStreamFrame(request_stream_id, Http2Frame::ErrorCode::Cancel);
    sendFrame(rst_frame);
  }

  if (http2FlowControlV2()) {
    test_server_->waitForGaugeEq("http2.streams_active", 1);
  }
  writev_matcher_->setResumeWrites();

  uint32_t last_stream = Http2Frame::makeClientStreamId(num_requests - 1);

  // Wait for a full response on |last_stream|
  std::map<uint32_t, ResponseInfo> response_info;
  parseResponseUntil(&response_info, [&]() -> bool {
    auto it = response_info.find(last_stream);
    return it != response_info.end() && it->second.end_stream_;
  });

  // The test does not wait for replies to be generated on all streams before sending out the resets
  // so we are only guaranteed that there will be a response on last_stream.
  EXPECT_LE(1, response_info.size());
  for (auto& [stream_id, info] : response_info) {
    if (stream_id == last_stream) {
      EXPECT_EQ(expected_response_size, response_info[stream_id].response_size_) << stream_id;
    } else {
      // Other streams may get a partial response.
      EXPECT_GT(expected_response_size, response_info[stream_id].response_size_) << stream_id;
      EXPECT_GT(2 * h2_stream_window, response_info[stream_id].response_size_) << stream_id;
    }
  }

  tcp_client_->close();
}

// Cover the case where the frame that triggered blocking was a header frame, and a RST_STREAM
// arrives for that stream.
TEST_P(Http2BufferWatermarksTest, RstStreamWhileBlockedProxyingHeaderFrame) {
  int num_requests = 5;
  uint64_t kNumBlocks = 4;
  uint64_t kBlockSize = 16 * 1024;
  uint32_t connection_watermark = 64 * 1024;
  uint32_t h2_stream_window = 64 * 1024;
  uint32_t h2_conn_window = 1024 * 1024 * 1024; // Effectively unlimited
  uint32_t expected_response_size = kNumBlocks * kBlockSize;

  initializeWithBufferConfig(
      {connection_watermark, h2_stream_window, h2_conn_window, h2_stream_window, h2_conn_window},
      num_requests, true);

  writev_matcher_->setWritevReturnsEgain();

  sendRequests(num_requests, kNumBlocks, kBlockSize, h2_stream_window);
  if (http2FlowControlV2()) {
    test_server_->waitForGaugeGe("http2.streams_active", 4);
  } else {
    test_server_->waitForGaugeEq("http2.streams_active", 0);
  }

  for (int32_t idx = 1; idx < num_requests - 1; ++idx) {
    const uint32_t request_stream_id = Http2Frame::makeClientStreamId(idx);
    auto rst_frame =
        Http2Frame::makeResetStreamFrame(request_stream_id, Http2Frame::ErrorCode::Cancel);
    sendFrame(rst_frame);
  }
  if (http2FlowControlV2()) {
    test_server_->waitForGaugeLe("http2.streams_active", 2);
  }

  writev_matcher_->setResumeWrites();

  uint32_t first_stream = Http2Frame::makeClientStreamId(0);
  uint32_t last_stream = Http2Frame::makeClientStreamId(num_requests - 1);

  std::map<uint32_t, ResponseInfo> response_info;
  parseResponse(&response_info, 2);
  if (http2FlowControlV2()) {
    EXPECT_GE(3, response_info.size());
    EXPECT_LE(2, response_info.size());
    for (auto& [stream_id, info] : response_info) {
      if (stream_id == first_stream || stream_id == last_stream) {
        EXPECT_EQ(expected_response_size, response_info[stream_id].response_size_) << stream_id;
      } else {
        EXPECT_EQ(0, response_info[stream_id].response_size_) << stream_id;
      }
    }
  } else {
    EXPECT_EQ(2, response_info.size());
    for (auto& [stream_id, info] : response_info) {
      EXPECT_EQ(expected_response_size, response_info[stream_id].response_size_) << stream_id;
    }
  }

  tcp_client_->close();
}

} // namespace Envoy
