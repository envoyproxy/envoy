#include <algorithm>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

#include "test/common/http/http2/http2_frame.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using Envoy::Http::Http2::Http2Frame;
using ::testing::HasSubstr;
using ::testing::MatchesRegex;

namespace Envoy {

namespace {
const int64_t TransmitThreshold = 100 * 1024 * 1024;
} // namespace

class Http2FloodMitigationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion,
                                               FakeHttpConnection::Type, Http::CodecClient::Type>>,
      public HttpIntegrationTest {
public:
  Http2FloodMitigationTest()
      : HttpIntegrationTest(std::get<2>(GetParam()), std::get<0>(GetParam())) {}

protected:
  void startHttp2Session();
  void floodServer(const Http2Frame& frame, const std::string& flood_stat);
  void floodServer(absl::string_view host, absl::string_view path,
                   Http2Frame::ResponseStatus expected_http_status, const std::string& flood_stat);
  Http2Frame readFrame();
  void sendFame(const Http2Frame& frame);
  void setNetworkConnectionBufferSize();
  void beginSession();

  IntegrationTcpClientPtr tcp_client_;
};

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
  setDownstreamProtocol(std::get<2>(GetParam()));
  setUpstreamProtocol(std::get<1>(GetParam()));
  // set lower outbound frame limits to make tests run faster
  config_helper_.setOutboundFramesLimits(1000, 100);
  initialize();
  tcp_client_ = makeTcpConnection(lookupPort("http"));
  startHttp2Session();
}

Http2Frame Http2FloodMitigationTest::readFrame() {
  Http2Frame frame;
  tcp_client_->waitForData(frame.HeaderSize);
  frame.setHeader(tcp_client_->data());
  tcp_client_->clearData(frame.HeaderSize);
  auto len = frame.payloadSize();
  if (len) {
    tcp_client_->waitForData(len);
    frame.setPayload(tcp_client_->data());
    tcp_client_->clearData(len);
  }
  return frame;
}

void Http2FloodMitigationTest::sendFame(const Http2Frame& frame) {
  ASSERT_TRUE(tcp_client_->connected());
  tcp_client_->write(std::string(frame), false, false);
}

void Http2FloodMitigationTest::startHttp2Session() {
  tcp_client_->write(Http2Frame::Preamble, false, false);

  // Send empty initial SETTINGS frame.
  auto settings = Http2Frame::makeEmptySettingsFrame();
  tcp_client_->write(std::string(settings), false, false);

  // Read initial SETTINGS frame from the server.
  readFrame();

  // Send an SETTINGS ACK.
  settings = Http2Frame::makeEmptySettingsFrame(Http2Frame::SettingsFlags::Ack);
  tcp_client_->write(std::string(settings), false, false);

  // read pending SETTINGS and WINDOW_UPDATE frames
  readFrame();
  readFrame();
}

// Verify that the server detects the flood of the given frame.
void Http2FloodMitigationTest::floodServer(const Http2Frame& frame, const std::string& flood_stat) {
  // pack the as many frames as we can into 16k buffer
  const int FrameCount = (16 * 1024) / frame.size();
  std::vector<char> buf(FrameCount * frame.size());
  for (auto pos = buf.begin(); pos != buf.end();) {
    pos = std::copy(frame.begin(), frame.end(), pos);
  }

  tcp_client_->readDisable(true);
  int64_t total_bytes_sent = 0;
  // If the flood protection is not working this loop will keep going
  // forever until it is killed by blaze timer or run out of memory.
  // Add early stop if we have sent more than 100M of frames, as it this
  // point it is obvious something is wrong.
  while (total_bytes_sent < TransmitThreshold && tcp_client_->connected()) {
    tcp_client_->write({buf.begin(), buf.end()}, false, false);
    total_bytes_sent += buf.size();
  }

  EXPECT_LE(total_bytes_sent, TransmitThreshold) << "Flood mitigation is broken.";
  EXPECT_EQ(1, test_server_->counter(flood_stat)->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

// Verify that the server detects the flood using specified request parameters.
void Http2FloodMitigationTest::floodServer(absl::string_view host, absl::string_view path,
                                           Http2Frame::ResponseStatus expected_http_status,
                                           const std::string& flood_stat) {
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(request_idx, host, path);
  sendFame(request);
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
  EXPECT_EQ(expected_http_status, frame.responseStatus());
  tcp_client_->readDisable(true);
  uint64_t total_bytes_sent = 0;
  while (total_bytes_sent < TransmitThreshold && tcp_client_->connected()) {
    request = Http2Frame::makeRequest(++request_idx, host, path);
    sendFame(request);
    total_bytes_sent += request.size();
  }
  EXPECT_LE(total_bytes_sent, TransmitThreshold) << "Flood mitigation is broken.";
  if (!flood_stat.empty()) {
    EXPECT_EQ(1, test_server_->counter(flood_stat)->value());
  }
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

INSTANTIATE_TEST_SUITE_P(
    IpVersions, Http2FloodMitigationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(FakeHttpConnection::Type::HTTP2,
                                     FakeHttpConnection::Type::LEGACY_HTTP2),
                     testing::ValuesIn(HTTP2_DOWNSTREAM)),
    HttpIntegrationTest::ipUpstreamDownstreamParamsToString);

TEST_P(Http2FloodMitigationTest, Ping) {
  setNetworkConnectionBufferSize();
  beginSession();
  floodServer(Http2Frame::makePingFrame(), "http2.outbound_control_flood");
}

TEST_P(Http2FloodMitigationTest, Settings) {
  setNetworkConnectionBufferSize();
  beginSession();
  floodServer(Http2Frame::makeEmptySettingsFrame(), "http2.outbound_control_flood");
}

// Verify that the server can detect flood of internally generated 404 responses.
TEST_P(Http2FloodMitigationTest, 404) {
  // Change the default route to be restrictive, and send a request to a non existent route.
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  beginSession();

  // Send requests to a non existent path to generate 404s
  floodServer("host", "/notfound", Http2Frame::ResponseStatus::NotFound, "http2.outbound_flood");
}

// Verify that the server can detect flood of DATA frames
TEST_P(Http2FloodMitigationTest, Data) {
  // Set large buffer limits so the test is not affected by the flow control.
  config_helper_.setBufferLimits(1024 * 1024 * 1024, 1024 * 1024 * 1024);
  autonomous_upstream_ = true;
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  floodServer("host", "/test/long/url", Http2Frame::ResponseStatus::Ok, "http2.outbound_flood");
}

// Verify that the server can detect flood of RST_STREAM frames.
TEST_P(Http2FloodMitigationTest, RST_STREAM) {
  // Use invalid HTTP headers to trigger sending RST_STREAM frames.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_http2_protocol_options()->set_stream_error_on_invalid_http_messaging(true);
      });
  beginSession();

  int i = 0;
  auto request = Http::Http2::Http2Frame::makeMalformedRequest(i);
  sendFame(request);
  auto response = readFrame();
  // Make sure we've got RST_STREAM from the server
  EXPECT_EQ(Http2Frame::Type::RstStream, response.type());
  uint64_t total_bytes_sent = 0;
  while (total_bytes_sent < TransmitThreshold && tcp_client_->connected()) {
    request = Http::Http2::Http2Frame::makeMalformedRequest(++i);
    sendFame(request);
    total_bytes_sent += request.size();
  }
  EXPECT_LE(total_bytes_sent, TransmitThreshold) << "Flood mitigation is broken.";
  EXPECT_EQ(1, test_server_->counter("http2.outbound_control_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
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
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Exceed the number of streams allowed by the server. The server should stop reading from the
  // client. Verify that the client was unable to stuff a lot of data into the server.
  floodServer("host", "/test/long/url", Http2Frame::ResponseStatus::Ok, "");
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

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeEmptyHeadersFrame(request_idx);
  sendFame(request);

  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.inbound_empty_frames_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

TEST_P(Http2FloodMitigationTest, EmptyHeadersContinuation) {
  beginSession();

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeEmptyHeadersFrame(request_idx);
  sendFame(request);

  for (int i = 0; i < 2; i++) {
    request = Http2Frame::makeEmptyContinuationFrame(request_idx);
    sendFame(request);
  }

  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.inbound_empty_frames_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

TEST_P(Http2FloodMitigationTest, EmptyData) {
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  uint32_t request_idx = 0;
  auto request = Http2Frame::makePostRequest(request_idx, "host", "/");
  sendFame(request);

  for (int i = 0; i < 2; i++) {
    request = Http2Frame::makeEmptyDataFrame(request_idx);
    sendFame(request);
  }

  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.inbound_empty_frames_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

TEST_P(Http2FloodMitigationTest, PriorityIdleStream) {
  beginSession();

  floodServer(Http2Frame::makePriorityFrame(0, 1), "http2.inbound_priority_frames_flood");
}

TEST_P(Http2FloodMitigationTest, PriorityOpenStream) {
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Open stream.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(request_idx, "host", "/");
  sendFame(request);

  floodServer(Http2Frame::makePriorityFrame(request_idx, request_idx + 1),
              "http2.inbound_priority_frames_flood");
}

TEST_P(Http2FloodMitigationTest, PriorityClosedStream) {
  autonomous_upstream_ = true;
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Open stream.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(request_idx, "host", "/");
  sendFame(request);
  // Reading response marks this stream as closed in nghttp2.
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());

  floodServer(Http2Frame::makePriorityFrame(request_idx, request_idx + 1),
              "http2.inbound_priority_frames_flood");
}

TEST_P(Http2FloodMitigationTest, WindowUpdate) {
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Open stream.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(request_idx, "host", "/");
  sendFame(request);

  floodServer(Http2Frame::makeWindowUpdateFrame(request_idx, 1),
              "http2.inbound_window_update_frames_flood");
}

// Verify that the HTTP/2 connection is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, ZerolenHeader) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  beginSession();

  // Send invalid request.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeMalformedRequestWithZerolenHeader(request_idx, "host", "/");
  sendFame(request);

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
        hcm.mutable_http2_protocol_options()->set_stream_error_on_invalid_http_messaging(true);
      });
  autonomous_upstream_ = true;
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Send invalid request.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeMalformedRequestWithZerolenHeader(request_idx, "host", "/");
  sendFame(request);
  // Make sure we've got RST_STREAM from the server.
  auto response = readFrame();
  EXPECT_EQ(Http2Frame::Type::RstStream, response.type());

  // Send valid request using the same connection.
  request_idx++;
  request = Http2Frame::makeRequest(request_idx, "host", "/");
  sendFame(request);
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

} // namespace Envoy
