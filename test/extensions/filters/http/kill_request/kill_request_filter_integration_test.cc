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
    config_helper_.prependFilter(filter_config);
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

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, KillRequestFilterIntegrationTestAllProtocols,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Request crash Envoy controlled via header configuration.
TEST_P(KillRequestFilterIntegrationTestAllProtocols, KillRequestCrashEnvoy) {
  initializeFilter(filter_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-envoy-kill-request", "true"}};

  EXPECT_DEATH(sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024),
               "KillRequestFilter is crashing Envoy!!!");
}

// Disabled for coverage per #18569
#if !defined(ENVOY_CONFIG_COVERAGE)
// KillRequestCrashEnvoyOnResponse is flaky on Windows
#ifndef WIN32
// Request crash Envoy controlled via response.
TEST_P(KillRequestFilterIntegrationTestAllProtocols, KillRequestCrashEnvoyOnResponse) {
  const std::string filter_config_response =
      R"EOF(
      name: envoy.filters.http.kill_request
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
        probability:
          numerator: 100
        direction: RESPONSE
      )EOF";
  initializeFilter(filter_config_response);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024);
  checkSimpleRequestSuccess(0, 1024, response.get());

  // Try with kill request header in response.
  Http::TestResponseHeaderMapImpl kill_response_headers = default_response_headers_;
  kill_response_headers.addCopy("x-envoy-kill-request", "true");

  EXPECT_DEATH(sendRequestAndWaitForResponse(request_headers, 0, kill_response_headers, 1024),
               "KillRequestFilter is crashing Envoy!!!");
}
#endif

TEST_P(KillRequestFilterIntegrationTestAllProtocols, KillRequestCrashEnvoyWithCustomKillHeader) {
  const std::string filter_config_with_custom_kill_header =
      R"EOF(
name: envoy.filters.http.kill_request
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
  probability:
    numerator: 100
  kill_request_header: "x-custom-kill-request"
)EOF";

  initializeFilter(filter_config_with_custom_kill_header);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-custom-kill-request", "true"}};

  EXPECT_DEATH(sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024),
               "KillRequestFilter is crashing Envoy!!!");
}
#endif

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
