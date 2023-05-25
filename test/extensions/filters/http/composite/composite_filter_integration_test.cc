#include <optional>
#include <string>

#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

#include "source/common/http/match_delegate/config.h"

#include "test/common/http/common.h"
#include "test/integration/filters/add_body_filter.pb.h"
#include "test/integration/filters/set_response_code_filter_config.pb.h"
#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

namespace Envoy {

namespace {

using envoy::config::route::v3::Route;
using envoy::config::route::v3::VirtualHost;
using envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute;
using envoy::extensions::filters::common::matcher::action::v3::SkipFilter;
using envoy::extensions::filters::http::composite::v3::ExecuteFilterAction;
using Envoy::Protobuf::MapPair;
using Envoy::ProtobufWkt::Any;
using envoy::type::matcher::v3::HttpRequestHeaderMatchInput;
using test::integration::filters::AddBodyFilterConfig;
using test::integration::filters::SetResponseCodeFilterConfig;
using xds::type::matcher::v3::Matcher_OnMatch;

class CompositeFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HttpIntegrationTest {
public:
  CompositeFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  ExtensionWithMatcherPerRoute createPerRouteConfig(
      std::function<void(envoy::config::core::v3::TypedExtensionConfig*)> base_action_function) {
    ExtensionWithMatcherPerRoute per_route_config;
    auto matcher_tree = per_route_config.mutable_xds_matcher()->mutable_matcher_tree();

    auto matcher_input = matcher_tree->mutable_input();
    matcher_input->set_name("request-headers");
    HttpRequestHeaderMatchInput match_input;
    match_input.set_header_name("match-header");
    matcher_input->mutable_typed_config()->PackFrom(match_input);

    auto map = matcher_tree->mutable_exact_match_map()->mutable_map();
    Matcher_OnMatch match;
    auto mutable_action = match.mutable_action();
    mutable_action->set_name("composite-action");

    ExecuteFilterAction filter_action;
    base_action_function(filter_action.mutable_typed_config());
    mutable_action->mutable_typed_config()->PackFrom(filter_action);

    (*map)["match"] = match;
    return per_route_config;
  }

  void addPerRouteResponseCodeFilter(const std::string& filter_name,
                                     const std::string& route_prefix, const int& code,
                                     const bool& response_prefix = false) {
    SetResponseCodeFilterConfig set_response_code;
    set_response_code.set_code(code);
    if (response_prefix) {
      set_response_code.set_prefix("skipLocalReplyAndContinue");
    }
    auto per_route_config = createPerRouteConfig([set_response_code](auto* cfg) {
      cfg->set_name("set-response-code-filter");
      cfg->mutable_typed_config()->PackFrom(set_response_code);
    });
    config_helper_.addConfigModifier(
        [per_route_config, filter_name, route_prefix](ConfigHelper::HttpConnectionManager& cm) {
          auto* vh = cm.mutable_route_config()->mutable_virtual_hosts(0);
          auto* route = vh->mutable_routes()->Mutable(0);
          route->mutable_match()->set_prefix(route_prefix);
          route->mutable_route()->set_cluster("cluster_0");
          (*route->mutable_typed_per_filter_config())[filter_name].PackFrom(per_route_config);
        });
  }

  void prependCompositeFilter(const std::string& name = "composite") {
    config_helper_.prependFilter(absl::StrFormat(R"EOF(
      name: %s
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: composite
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
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
                    name: composite-action
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.ExecuteFilterAction
                      typed_config:
                        name: set-response-code
                        typed_config:
                          "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
                          code: 403
    )EOF",
                                                 name));
  }

  const Http::TestRequestHeaderMapImpl match_request_headers_ = {{":method", "GET"},
                                                                 {":path", "/somepath"},
                                                                 {":scheme", "http"},
                                                                 {"match-header", "match"},
                                                                 {":authority", "blah"}};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CompositeFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that if we don't match the match action the request is proxied as normal, while if the
// match action is hit we apply the specified filter to the stream.
TEST_P(CompositeFilterIntegrationTest, TestBasic) {
  prependCompositeFilter();
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  {
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  }

  {
    auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("403"));
  }
}

// Verifies function of the per-route config in the ExtensionWithMatcher class.
TEST_P(CompositeFilterIntegrationTest, TestPerRoute) {
  prependCompositeFilter();
  addPerRouteResponseCodeFilter(/*filter_name=*/"composite", /*route_prefix=*/"/somepath",
                                /*code=*/401);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("401"));
}

// Test an empty match tree resolving with a per route config.
TEST_P(CompositeFilterIntegrationTest, TestPerRouteEmptyMatcher) {
  config_helper_.prependFilter(R"EOF(
      name: composite
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: composite
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
    )EOF");
  addPerRouteResponseCodeFilter(/*filter_name=*/"composite", /*route_prefix=*/"/somepath",
                                /*code=*/402);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("402"));
}

// Test that the specified filters apply per route configs to requests.
TEST_P(CompositeFilterIntegrationTest, TestPerRouteMultipleFilters) {
  prependCompositeFilter(/*name=*/"composite_2");
  prependCompositeFilter();

  addPerRouteResponseCodeFilter(/*filter_name=*/"composite", /*route_prefix=*/"/somepath",
                                /*code=*/407, /*response_prefix=*/true);
  addPerRouteResponseCodeFilter(/*filter_name=*/"composite_2", /*route_prefix=*/"/somepath",
                                /*code=*/402);

  initialize();
  {

    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("402"));
  }

  cleanupUpstreamAndDownstream();

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    const Http::TestRequestHeaderMapImpl custom_request_headers_ = {{":method", "POST"},
                                                                    {":path", "/otherpath"},
                                                                    {":scheme", "http"},
                                                                    {"match-header", "match"},
                                                                    {":authority", "blah"}};
    auto response = codec_client_->makeRequestWithBody(custom_request_headers_, 1024);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("403"));
  }
}
} // namespace
} // namespace Envoy
