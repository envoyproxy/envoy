#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class UriTemplateInputMatcherIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  UriTemplateInputMatcherIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    // The wrapped filter replies with a 403 unless the path matches the template.
    config_helper_.prependFilter(R"EOF(
name: match_delegate_filter
typed_config:
  "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
  extension_config:
    name: set-response-code
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
      code: 403
  xds_matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.inputs.request_headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: ":path"
            custom_match:
              name: envoy.matching.matchers.uri_template
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
                path_template: "/api/{version}/items/{id}"
        on_match:
          action:
            name: skip
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");
    HttpIntegrationTest::initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UriTemplateInputMatcherIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UriTemplateInputMatcherIntegrationTest, TemplateMatchSkipsFilter) {
  initialize();
  default_request_headers_.setPath("/api/v1/items/123?limit=1");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(UriTemplateInputMatcherIntegrationTest, TemplateMissAppliesFilter) {
  initialize();
  default_request_headers_.setPath("/api/v1/items/123/extra");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

} // namespace
} // namespace Envoy
