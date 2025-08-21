#include "envoy/config/core/v3/base.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"

#include "test/extensions/common/tap/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TapIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  TapIntegrationTest()
      // Note: This test must use HTTP/2 because of the lack of early close detection for
      // HTTP/1 on OSX. In this test we close the admin /tap stream when we don't want any
      // more data, and without immediate close detection we can't have a flake free test.
      // Thus, we use HTTP/2 for everything here.
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {

    // Also use HTTP/2 for upstream so that we can fully test trailers.
    setUpstreamProtocol(Http::CodecType::HTTP2);

    Envoy::Logger::DelegatingLogSinkSharedPtr sink_ptr = Envoy::Logger::Registry::getSink();
    sink_ptr->setShouldEscape(false);
  }

  void initializeFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config);
    initialize();
  }

  std::pair<Http::RequestEncoder*, IntegrationStreamDecoderPtr>
  startRequest(const Http::TestRequestHeaderMapImpl& request_headers,
               const std::vector<std::string>& request_body_chunks,
               const Http::TestRequestTrailerMapImpl* request_trailers,
               IntegrationCodecClient* codec_client) {
    if (!request_trailers && request_body_chunks.empty()) {
      // Headers only request - no encoder needed as no data
      return {nullptr, codec_client->makeHeaderOnlyRequest(request_headers)};
    }

    auto encoder_decoder = codec_client->startRequest(request_headers);
    return {&encoder_decoder.first, std::move(encoder_decoder.second)};
  }

  void encodeRequest(const std::vector<std::string>& request_body_chunks,
                     const Http::TestRequestTrailerMapImpl* request_trailers,
                     Http::RequestEncoder* encoder) {
    if (!encoder || (!request_trailers && request_body_chunks.empty())) {
      return;
    }

    // Encode each chunk of body data
    for (size_t i = 0; i < request_body_chunks.size(); i++) {
      Buffer::OwnedImpl data(request_body_chunks[i]);
      bool endStream = i == (request_body_chunks.size() - 1) && !request_trailers;
      encoder->encodeData(data, endStream);
    }

    // Encode trailers if they exist
    if (request_trailers) {
      encoder->encodeTrailers(*request_trailers);
    }
  }

  void encodeResponse(const Http::TestResponseHeaderMapImpl& response_headers,
                      const std::vector<std::string>& response_body_chunks,
                      const Http::TestResponseTrailerMapImpl* response_trailers,
                      FakeStream* upstream_request, IntegrationStreamDecoderPtr& decoder) {
    upstream_request->encodeHeaders(response_headers,
                                    !response_trailers && response_body_chunks.empty());

    for (size_t i = 0; i < response_body_chunks.size(); i++) {
      Buffer::OwnedImpl data(response_body_chunks[i]);
      bool endStream = i == (response_body_chunks.size() - 1) && !response_trailers;
      upstream_request->encodeData(data, endStream);
    }

    if (response_trailers) {
      upstream_request->encodeTrailers(*response_trailers);
    }

    ASSERT_TRUE(decoder->waitForEndStream());
  }

  void makeRequest(const Http::TestRequestHeaderMapImpl& request_headers,
                   const std::vector<std::string>& request_body_chunks,
                   const Http::TestRequestTrailerMapImpl* request_trailers,
                   const Http::TestResponseHeaderMapImpl& response_headers,
                   const std::vector<std::string>& response_body_chunks,
                   const Http::TestResponseTrailerMapImpl* response_trailers) {
    auto [encoder, decoder] =
        startRequest(request_headers, request_body_chunks, request_trailers, codec_client_.get());
    encodeRequest(request_body_chunks, request_trailers, encoder);
    waitForNextUpstreamRequest();
    encodeResponse(response_headers, response_body_chunks, response_trailers,
                   upstream_request_.get(), decoder);
  }

  std::string getTempPathPrefix() {
    const std::string path_prefix = TestEnvironment::temporaryDirectory() + "/tap_integration_" +
                                    testing::UnitTest::GetInstance()->current_test_info()->name();
    TestEnvironment::createPath(path_prefix);
    return path_prefix + "/";
  }

  const Http::TestRequestHeaderMapImpl request_headers_udp_tap_{{":method", "GET"},
                                                                {":path", "/tapudp"},
                                                                {":scheme", "http"},
                                                                {":authority", "host"},
                                                                {"foo", "bar"}};
  const Http::TestResponseHeaderMapImpl response_headers_udp_tap_{{":status", "200"},
                                                                  {"bar", "baz"}};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TapIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TapIntegrationTest, StaticExtTapSinkUdp) {
  constexpr absl::string_view filter_config =
      R"EOF(
name: tap
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
  common_config:
    static_config:
      match:
        any_match: true
      output_config:
        sinks:
          - format: JSON_BODY_AS_STRING
            custom_sink:
              name: cfx_custom_sink
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.tap_sinks.udp_sink.v3alpha.UdpSink
                udp_address:
                  protocol: UDP
                  address: 127.0.0.1
                  port_value: 8089
)EOF";

  // Define UDP server.
  class TapUdpServer {
  public:
    bool startUDPServer(void) {
      server_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
      if (server_socket_ < 0) {
        return false;
      }
      int flags = fcntl(server_socket_, F_GETFL, 0);
      if (flags < 0 || fcntl(server_socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(server_socket_);
        return false;
      }
      sockaddr_in server_addr;
      memset(&server_addr, 0, sizeof(server_addr));
      server_addr.sin_family = AF_INET;
      if (inet_pton(AF_INET, UDP_SERVER_IP_, &server_addr.sin_addr) <= 0) {
        close(server_socket_);
        return false;
      }
      server_addr.sin_port = htons(UDP_PORT_);
      if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&server_addr),
               sizeof(server_addr)) < 0) {
        close(server_socket_);
        return false;
      }
      return true;
    }

    void checkRcvedUDPMsg() {
      const int UDP_RCV_BUFFER_SIZE_ = 3072;
      char buffer[UDP_RCV_BUFFER_SIZE_] = {0};
      sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      memset(buffer, 0, UDP_RCV_BUFFER_SIZE_);
      ssize_t bytes_received =
          recvfrom(server_socket_, buffer, UDP_RCV_BUFFER_SIZE_, 0,
                   reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
      if (bytes_received <= 0) {
        // Return false because there is no any message.
        // Consider other logical if EWOULDBLOCK is occurred, and
        // shouldn't occur in UT env
        isRcvMatchedUDPMsg_ = false;
      } else {
        // Go the message.
        std::string rcv_msg{buffer};
        if (rcv_msg.find(MATCHED_TAP_REQ_STR_) != std::string::npos &&
            rcv_msg.find(MATCHED_TAP_RESP_STR_) != std::string::npos) {
          isRcvMatchedUDPMsg_ = true;
        }
      }
    }

    void stopUDPServer(void) {
      close(server_socket_);
      server_socket_ = -1;
    }

    bool isUDPServerRcvMatchedUDPMsg(void) { return isRcvMatchedUDPMsg_; }

  private:
    const int UDP_PORT_ = 8089;
    const char* UDP_SERVER_IP_ = "127.0.0.1";
    const char* MATCHED_TAP_REQ_STR_ = "tapudp";
    const char* MATCHED_TAP_RESP_STR_ = "200";
    int server_socket_ = -1;
    bool isRcvMatchedUDPMsg_ = false;
  };

  // Start UDP server firstly.
  TapUdpServer tap_server;
  // Init UDP server to create UDP socket and set socket to non-block mode.
  EXPECT_TRUE(tap_server.startUDPServer());

  // Start HTTP test.
  initializeFilter(fmt::format(filter_config, getTempPathPrefix()));
  // Initial request/response with tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_udp_tap_, {}, nullptr, response_headers_udp_tap_, {}, nullptr);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  // Verify whether get the expect message
  tap_server.checkRcvedUDPMsg();
  tap_server.stopUDPServer();
  EXPECT_TRUE(tap_server.isUDPServerRcvMatchedUDPMsg());
}

} // namespace
} // namespace Envoy
