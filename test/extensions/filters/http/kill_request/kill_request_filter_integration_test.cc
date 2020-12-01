#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {
namespace {

class KillRequestFilterIntegrationTest : public Event::TestUsingSimulatedTime,
                                         public HttpProtocolIntegrationTest {
protected:
  void initializeFilter(const std::string& filter_config) {
    config_helper_.addFilter(filter_config);
    initialize();
  }

  const std::string filter_config_ =
      R"EOF(
name: envoy.filters.http.kill_request
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
  probability:
    numerator: 100
)EOF";
};

// Tests should run with all protocols.
class KillRequestFilterIntegrationTestAllProtocols : public KillRequestFilterIntegrationTest {};
INSTANTIATE_TEST_SUITE_P(Protocols, KillRequestFilterIntegrationTestAllProtocols,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Request abort controlled via header configuration.
TEST_P(KillRequestFilterIntegrationTestAllProtocols, KillRequestCrashEnvoy) {
  initializeFilter(filter_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-envoy-kill-request", "true"}};

  EXPECT_DEATH(sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024),
               "");
}

TEST_P(KillRequestFilterIntegrationTestAllProtocols, KillRequestDisabledWhenHeaderIsMissing) {
  initializeFilter(filter_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 1024);
}

TEST_P(KillRequestFilterIntegrationTestAllProtocols, KillRequestDisabledWhenHeaderValueIsInvalid) {
  initializeFilter(filter_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-envoy-kill-request", "invalid"}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024);
}

TEST_P(KillRequestFilterIntegrationTestAllProtocols, KillRequestDisabledByZeroProbability) {
  const std::string zero_probability_filter_config =
      R"EOF(
name: envoy.filters.http.kill_request
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
  probability:
    numerator: 0
)EOF";

  initializeFilter(zero_probability_filter_config);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-envoy-kill-request", "true"}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024);
}

} // namespace
} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
