#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class CompositeFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HttpIntegrationTest {
public:
  CompositeFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addFilter(R"EOF(
  name: composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
    extension_config:
      name: composite
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
    matcher:
      matcher_tree:
        input:
          name: request-headers
          typed_config:
            "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
            header_name: match-header
        exact_match_map:
          map:
            match:
              action:
                name: composite-action
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.ExecuteFilterAction
                  typed_config:
                    name: set-response-code
                    typed_config:
                      "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
                      code: 403
    )EOF");
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CompositeFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that if we don't match the match action the request is proxied as normal, while if the
// match action is hit we apply the specified filter to the stream.
TEST_P(CompositeFilterIntegrationTest, TestBasic) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  {
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    response->waitForEndStream();
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  }

  {
    const Http::TestRequestHeaderMapImpl request_headers = {{":method", "GET"},
                                                            {":path", "/somepath"},
                                                            {":scheme", "http"},
                                                            {"match-header", "match"},
                                                            {":authority", "blah"}};
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    response->waitForEndStream();
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("403"));
  }
}

} // namespace Envoy
