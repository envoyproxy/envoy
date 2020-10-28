#include <algorithm>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/random_generator.h"
#include "common/http/header_map_impl.h"
#include "common/network/socket_option_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/filters/test_socket_interface.h"
#include "test/integration/http_integration.h"
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
} // namespace

class SocketInterfaceSwap {
public:
  // Object of this class hold the state determining the IoHandle which
  // should return EAGAIN from the `writev` call.
  struct IoHandleMatcher {
    bool shouldReturnEgain(uint32_t port) const {
      absl::ReaderMutexLock lock(&mutex_);
      return port == port_ && writev_returns_egain_;
    }

    void setSourcePort(uint32_t port) {
      absl::WriterMutexLock lock(&mutex_);
      port_ = port;
    }

    void setWritevReturnsEgain() {
      absl::WriterMutexLock lock(&mutex_);
      writev_returns_egain_ = true;
    }

  private:
    mutable absl::Mutex mutex_;
    uint32_t port_ ABSL_GUARDED_BY(mutex_) = 0;
    bool writev_returns_egain_ ABSL_GUARDED_BY(mutex_) = false;
  };

  SocketInterfaceSwap() {
    Envoy::Network::SocketInterfaceSingleton::clear();
    test_socket_interface_loader_ = std::make_unique<Envoy::Network::SocketInterfaceLoader>(
        std::make_unique<Envoy::Network::TestSocketInterface>(
            [writev_matcher = writev_matcher_](
                Envoy::Network::TestIoSocketHandle* io_handle, const Buffer::RawSlice*,
                uint64_t) -> absl::optional<Api::IoCallUint64Result> {
              if (writev_matcher->shouldReturnEgain(io_handle->localAddress()->ip()->port())) {
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
class Http2FloodMitigationTest : public SocketInterfaceSwap,
                                 public testing::TestWithParam<Network::Address::IpVersion>,
                                 public Http2RawFrameIntegrationTest {
public:
  Http2FloodMitigationTest() : Http2RawFrameIntegrationTest(GetParam()) {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(1); });
  }

protected:
  std::vector<char> serializeFrames(const Http2Frame& frame, uint32_t num_frames);
  void floodServer(const Http2Frame& frame, const std::string& flood_stat, uint32_t num_frames);
  void floodServer(absl::string_view host, absl::string_view path,
                   Http2Frame::ResponseStatus expected_http_status, const std::string& flood_stat,
                   uint32_t num_frames);

  void setNetworkConnectionBufferSize();
  void beginSession() override;
  void prefillOutboundDownstreamQueue(uint32_t data_frame_count, uint32_t data_frame_size = 10);
  void triggerListenerDrain();
};

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2FloodMitigationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

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
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  // set lower outbound frame limits to make tests run faster
  config_helper_.setDownstreamOutboundFramesLimits(AllFrameFloodLimit, ControlFrameFloodLimit);
  initialize();
  // Set up a raw connection to easily send requests without reading responses. Also, set a small
  // TCP receive buffer to speed up connection backup.
  auto options = std::make_shared<Network::Socket::Options>();
  options->emplace_back(std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_RCVBUF), 1024));
  writev_matcher_->setSourcePort(lookupPort("http"));
  tcp_client_ = makeTcpConnection(lookupPort("http"), options);
  startHttp2Session();
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
  test_server_->waitForCounterGe("http.config_test.downstream_cx_delayed_close_timeout", 1);
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

  const auto request = Http2Frame::makeRequest(
      1, "host", "/test/long/url",
      {Http2Frame::Header("response_data_blocks", "1000"), Http2Frame::Header("no_trailers", "0")});
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
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->mutable_http2_protocol_options()->set_allow_metadata(true);
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
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.new_codec_behavior")) {
    // Upstream flood checks are not implemented in the old codec based on exceptions
    return;
  }
  config_helper_.addRuntimeOverride("envoy.reloadable_features.upstream_http2_flood_checks",
                                    "true");
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  // set lower upstream outbound frame limits to make tests run faster
  config_helper_.setUpstreamOutboundFramesLimits(AllFrameFloodLimit, ControlFrameFloodLimit);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  auto* upstream = fake_upstreams_.front().get();
  // Make Envoy's writes into the upstream connection to return EAGAIN
  writev_matcher_->setSourcePort(
      fake_upstream_connection_->connection().remoteAddress()->ip()->port());

  auto buf = serializeFrames(Http2Frame::makePingFrame(), ControlFrameFloodLimit + 1);

  writev_matcher_->setWritevReturnsEgain();
  ASSERT_TRUE(upstream->rawWriteConnection(0, std::string(buf.begin(), buf.end())));

  // Upstream connection should be disconnected
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  // Downstream client should receive 503 since upstream did not send response headers yet
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.outbound_control_flood")->value());
}

} // namespace Envoy
