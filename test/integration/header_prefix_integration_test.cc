#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/integration/server.h"

#include "gtest/gtest.h"

namespace Envoy {

// Unfortunately in the Envoy test suite, the headers singleton is initialized
// well before server start-up, so by the time the server has parsed the
// bootstrap proto it's too late to set it.
//
// Instead, set the value early and regression test the bootstrap proto's validation of prefix
// injection. We also register a custom header to make sure that registered headers interact well
// with the prefix override.
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    cache_control_handle(Http::CustomHeaders::get().CacheControl);

static const char* custom_prefix_ = "x-custom";

class HeaderPrefixIntegrationTest : public HttpProtocolIntegrationTest {
public:
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    ThreadSafeSingleton<Http::PrefixValue>::get().setPrefix(custom_prefix_);
  }
};

TEST_P(HeaderPrefixIntegrationTest, CustomHeaderPrefix) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.set_header_prefix("x-custom");
  });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  EXPECT_FALSE(response->headers()
                   .get(Envoy::Http::LowerCaseString{"x-custom-upstream-service-time"})
                   .empty());
  EXPECT_EQ("x-custom-upstream-service-time",
            response->headers().EnvoyUpstreamServiceTime()->key().getStringView());

  EXPECT_FALSE(upstream_request_->headers()
                   .get(Envoy::Http::LowerCaseString{"x-custom-expected-rq-timeout-ms"})
                   .empty());
  EXPECT_EQ("x-custom-expected-rq-timeout-ms",
            upstream_request_->headers().EnvoyExpectedRequestTimeoutMs()->key().getStringView());
}

// In this case, the header prefix set in the bootstrap will not match the
// singleton header prefix in SetUpTestSuite, and Envoy will RELEASE_ASSERT on
// start-up.
TEST_P(HeaderPrefixIntegrationTest, FailedCustomHeaderPrefix) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.set_header_prefix("x-custom-but-not-set");
  });
  EXPECT_DEATH(initialize(), "Attempting to change the header prefix after it has been used!");
}

INSTANTIATE_TEST_SUITE_P(Protocols, HeaderPrefixIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);
} // namespace Envoy
