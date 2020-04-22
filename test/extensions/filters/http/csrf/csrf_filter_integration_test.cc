#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {
const std::string CSRF_ENABLED_CONFIG = R"EOF(
name: csrf
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.csrf.v2.CsrfPolicy
  filter_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
  shadow_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
)EOF";

const std::string CSRF_FILTER_ENABLED_CONFIG = R"EOF(
name: csrf
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.csrf.v2.CsrfPolicy
  filter_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
)EOF";

const std::string CSRF_SHADOW_ENABLED_CONFIG = R"EOF(
name: csrf
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.csrf.v2.CsrfPolicy
  filter_enabled:
    default_value:
      numerator: 0
      denominator: HUNDRED
  shadow_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
)EOF";

const std::string CSRF_DISABLED_CONFIG = R"EOF(
name: csrf
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.csrf.v2.CsrfPolicy
  filter_enabled:
    default_value:
      numerator: 0
      denominator: HUNDRED
)EOF";

class CsrfFilterIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  IntegrationStreamDecoderPtr
  sendRequestAndWaitForResponse(Http::RequestHeaderMap& request_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    response->waitForEndStream();

    return response;
  }

  IntegrationStreamDecoderPtr sendRequest(Http::TestRequestHeaderMapImpl& request_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    response->waitForEndStream();

    return response;
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, CsrfFilterIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(CsrfFilterIntegrationTest, TestCsrfSuccess) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "localhost"},
      {"host", "localhost"},
  }};
  const auto& response = sendRequestAndWaitForResponse(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "200");
}

TEST_P(CsrfFilterIntegrationTest, TestCsrfDisabled) {
  config_helper_.addFilter(CSRF_DISABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  }};
  const auto& response = sendRequestAndWaitForResponse(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "200");
}

TEST_P(CsrfFilterIntegrationTest, TestNonMutationMethod) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  }};
  const auto& response = sendRequestAndWaitForResponse(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "200");
}

TEST_P(CsrfFilterIntegrationTest, TestOriginMismatch) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  }};
  const auto& response = sendRequest(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "403");
}

TEST_P(CsrfFilterIntegrationTest, TestEnforcesPost) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{
      {":method", "POST"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  }};
  const auto& response = sendRequest(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "403");
}

TEST_P(CsrfFilterIntegrationTest, TestEnforcesDelete) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{
      {":method", "DELETE"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  }};
  const auto& response = sendRequest(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "403");
}

TEST_P(CsrfFilterIntegrationTest, TestEnforcesPatch) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{
      {":method", "PATCH"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  }};
  const auto& response = sendRequest(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "403");
}

TEST_P(CsrfFilterIntegrationTest, TestRefererFallback) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{":method", "DELETE"},
                                            {":path", "/"},
                                            {":scheme", "http"},
                                            {"referer", "test-origin"},
                                            {"host", "test-origin"}};
  const auto& response = sendRequestAndWaitForResponse(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "200");
}

TEST_P(CsrfFilterIntegrationTest, TestMissingOrigin) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {
      {{":method", "DELETE"}, {":path", "/"}, {":scheme", "http"}, {"host", "test-origin"}}};
  const auto& response = sendRequest(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "403");
}

TEST_P(CsrfFilterIntegrationTest, TestShadowOnlyMode) {
  config_helper_.addFilter(CSRF_SHADOW_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "localhost"},
  }};
  const auto& response = sendRequestAndWaitForResponse(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "200");
}

TEST_P(CsrfFilterIntegrationTest, TestFilterAndShadowEnabled) {
  config_helper_.addFilter(CSRF_ENABLED_CONFIG);
  Http::TestRequestHeaderMapImpl headers = {{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "localhost"},
  }};
  const auto& response = sendRequest(headers);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().Status()->value().getStringView(), "403");
}
} // namespace
} // namespace Envoy
