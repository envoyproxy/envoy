#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"

#include "source/common/http/match_delegate/config.h"

#include "test/integration/filters/set_response_code_filter_config.pb.h"
#include "test/integration/http_integration.h"
#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchDelegate {
namespace {

using envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute;
using Envoy::Protobuf::MapPair;
using Envoy::ProtobufWkt::Any;

class MatchDelegateInegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  MatchDelegateInegrationTest() : HttpIntegrationTest(Envoy::Http::CodecType::HTTP1, GetParam()) {}
  void initialize() override {
    config_helper_.prependFilter(default_config_);
    HttpIntegrationTest::initialize();

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  }

  const Envoy::Http::TestRequestHeaderMapImpl default_request_headers_ = {{":method", "GET"},
                                                                          {":path", "/test"},
                                                                          {":scheme", "http"},
                                                                          {"match-header", "route"},
                                                                          {":authority", "host"}};

  const std::string default_config_ = R"EOF(
      name: match_delegate_filter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: set-response-code
          typed_config:
            "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
            code: 403
    )EOF";
  const std::string per_route_config_ = R"EOF(
      xds_matcher:
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
                  name: skip
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
    )EOF";
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MatchDelegateInegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(MatchDelegateInegrationTest, NoMatcherDefault) {
  initialize();
  Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, response_headers, 0);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("200"));
}

TEST_P(MatchDelegateInegrationTest, PerRouteConfig) {
  config_helper_.addConfigModifier([this](ConfigHelper::HttpConnectionManager& cm) {
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_route()->set_cluster("cluster_0");
    route->mutable_match()->set_prefix("/test");
    const auto matcher = TestUtility::parseYaml<ExtensionWithMatcherPerRoute>(per_route_config_);
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(matcher));
    route->mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("match_delegate_filter", cfg_any));
  });
  initialize();
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("403"));
}

} // namespace
} // namespace MatchDelegate
} // namespace Http
} // namespace Common
} // namespace Envoy
