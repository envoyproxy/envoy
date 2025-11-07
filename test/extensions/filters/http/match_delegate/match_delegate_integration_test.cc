#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"

#include "source/extensions/filters/http/match_delegate/config.h"

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
using Envoy::Protobuf::Any;
using Envoy::Protobuf::MapPair;

class MatchDelegateIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  MatchDelegateIntegrationTest() : HttpIntegrationTest(Envoy::Http::CodecType::HTTP1, GetParam()) {}
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

INSTANTIATE_TEST_SUITE_P(IpVersions, MatchDelegateIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(MatchDelegateIntegrationTest, NoMatcherDefault) {
  initialize();
  Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, response_headers, 0);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(MatchDelegateIntegrationTest, PerRouteConfig) {
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
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Verify that per-route config with HttpResponseHeaderMatchInput is accepted and loads correctly.
TEST_P(MatchDelegateIntegrationTest, PerRouteConfigResponseHeaders) {
  const std::string per_route_response_header_config = R"EOF(
      xds_matcher:
        matcher_tree:
          input:
            name: response-headers
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseHeaderMatchInput
              header_name: match-response-header
          exact_match_map:
            map:
              match:
                action:
                  name: skip
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
    )EOF";

  config_helper_.addConfigModifier([&](ConfigHelper::HttpConnectionManager& cm) {
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_route()->set_cluster("cluster_0");
    route->mutable_match()->set_prefix("/test");
    const auto matcher =
        TestUtility::parseYaml<ExtensionWithMatcherPerRoute>(per_route_response_header_config);
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(matcher));
    route->mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("match_delegate_filter", cfg_any));
  });

  // initialize() should succeed without throwing an exception.
  // Before the changes, the response header matcher would be rejected by the allowlist,
  // causing the match tree to be empty and the filter to be silently skipped.
  EXPECT_NO_THROW(initialize());

  // Send a basic request to verify the configuration is functional.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  // The filter should apply since we're not sending matching response headers from upstream.
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Verify that per-route config with HttpResponseTrailerMatchInput is accepted and loads correctly.
TEST_P(MatchDelegateIntegrationTest, PerRouteConfigResponseTrailers) {
  const std::string per_route_response_trailer_config = R"EOF(
      xds_matcher:
        matcher_tree:
          input:
            name: response-trailers
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseTrailerMatchInput
              header_name: match-response-trailer
          exact_match_map:
            map:
              match:
                action:
                  name: skip
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
    )EOF";

  config_helper_.addConfigModifier([&](ConfigHelper::HttpConnectionManager& cm) {
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_route()->set_cluster("cluster_0");
    route->mutable_match()->set_prefix("/test");
    const auto matcher =
        TestUtility::parseYaml<ExtensionWithMatcherPerRoute>(per_route_response_trailer_config);
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(matcher));
    route->mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("match_delegate_filter", cfg_any));
  });

  // initialize() should succeed without throwing an exception.
  // Before the changes, the response trailer matcher would be rejected by the allowlist,
  // causing the match tree to be empty and the filter to be silently skipped.
  EXPECT_NO_THROW(initialize());

  // Send a basic request to verify the configuration is functional.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  // The filter should apply since we're not sending matching response trailers from upstream.
  EXPECT_EQ("403", response->headers().getStatusValue());
}

} // namespace
} // namespace MatchDelegate
} // namespace Http
} // namespace Common
} // namespace Envoy
