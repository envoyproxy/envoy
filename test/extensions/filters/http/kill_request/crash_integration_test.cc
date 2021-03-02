#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"

#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

class CrashIntegrationTest : public Event::TestUsingSimulatedTime,
                             public HttpProtocolIntegrationTest {
protected:
  void initializeFilter(const std::string& filter_config) {
    config_helper_.addFilter(filter_config);
    initialize();
  }
};

// Tests should run with all protocols.
class CrashIntegrationTestAllProtocols : public CrashIntegrationTest {};
INSTANTIATE_TEST_SUITE_P(Protocols, CrashIntegrationTestAllProtocols,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(CrashIntegrationTestAllProtocols, UnwindsTrackedObjectStack) {
  const std::string request_kill_config =
      R"EOF(
      name: envoy.filters.http.kill_request
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
        probability:
          numerator: 100
      )EOF";
  initializeFilter(request_kill_config);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test"},
                                                       {":scheme", "http"},
                                                       {":authority", "host"},
                                                       {"x-envoy-kill-request", "true"}};
  // We should have the following directly on the tracked object stack and should dump them on
  // crash:
  //  - ActiveStream
  //  - Http(1|2)::ConnectionImpl
  //  - Network::ConnectionImpl
  const std::string death_string = GetParam().downstream_protocol == Http::CodecClient::Type::HTTP2
                                       ? "ActiveStream.*Http2::ConnectionImpl.*ConnectionImpl"
                                       : "ActiveStream.*Http1::ConnectionImpl.*ConnectionImpl";
  EXPECT_DEATH(sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024),
               death_string);
}

TEST_P(CrashIntegrationTestAllProtocols, ResponseCrashDumpsTheCorrespondingRequest) {
  const std::string response_kill_config =
      R"EOF(
      name: envoy.filters.http.kill_request
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
        probability:
          numerator: 100
        direction: RESPONSE
      )EOF";
  initializeFilter(response_kill_config);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  const Http::TestResponseHeaderMapImpl kill_response_headers = {{":status", "200"},
                                                                 {"x-envoy-kill-request", "true"}};
  // Check that we dump the downstream request
  EXPECT_DEATH(
      sendRequestAndWaitForResponse(default_request_headers_, 0, kill_response_headers, 1024),
      "Dumping corresponding downstream request.*UpstreamRequest.*request_headers:");
}

} // namespace Envoy
