#include "source/extensions/filters/http/header_mutation/config.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {
namespace {

class HeaderMutationIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  HeaderMutationIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initializeFilter() {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);

    config_helper_.prependFilter(R"EOF(
name: donwstream-header-mutation
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
  mutations:
    request_mutations:
    - append:
        header:
          key: "downstream-request-global-flag-header"
          value: "downstream-request-global-flag-header-value"
        append_action: APPEND_IF_EXISTS_OR_ADD
    response_mutations:
    - append:
        header:
          key: "downstream-global-flag-header"
          value: "downstream-global-flag-header-value"
        append_action: APPEND_IF_EXISTS_OR_ADD
)EOF",
                                 true);

    config_helper_.prependFilter(R"EOF(
name: downstream-header-mutation-disabled-by-default
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
  mutations:
    request_mutations:
    - append:
        header:
          key: "downstream-request-global-flag-header-disabled-by-default"
          value: "downstream-request-global-flag-header-value-disabled-by-default"
        append_action: APPEND_IF_EXISTS_OR_ADD
    response_mutations:
    - append:
        header:
          key: "downstream-global-flag-header-disabled-by-default"
          value: "downstream-global-flag-header-value-disabled-by-default"
        append_action: APPEND_IF_EXISTS_OR_ADD
disabled: true
)EOF",
                                 true);

    config_helper_.prependFilter(R"EOF(
name: upstream-header-mutation
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
  mutations:
    request_mutations:
    - append:
        header:
          key: "upstream-request-global-flag-header"
          value: "upstream-request-global-flag-header-value"
        append_action: APPEND_IF_EXISTS_OR_ADD
    response_mutations:
    - append:
        header:
          key: "upstream-global-flag-header"
          value: "upstream-global-flag-header-value"
        append_action: APPEND_IF_EXISTS_OR_ADD
    - append:
        header:
          key: "request-method-in-upstream-filter"
          value: "%REQ(:METHOD)%"
        append_action: APPEND_IF_EXISTS_OR_ADD
)EOF",
                                 false);

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);

          // Another route that disables downstream header mutation.
          auto* another_route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_routes();
          *another_route = *route;

          route->mutable_match()->set_path("/default/route");
          another_route->mutable_match()->set_path("/disable/filter/route");

          {
            // Per route header mutation for downstream.
            PerRouteProtoConfig header_mutation;
            auto response_mutation =
                header_mutation.mutable_mutations()->mutable_response_mutations()->Add();
            response_mutation->mutable_append()->mutable_header()->set_key(
                "downstream-per-route-flag-header");
            response_mutation->mutable_append()->mutable_header()->set_value(
                "downstream-per-route-flag-header-value");
            response_mutation->mutable_append()->set_append_action(
                envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
            ProtobufWkt::Any per_route_config;
            per_route_config.PackFrom(header_mutation);
            route->mutable_typed_per_filter_config()->insert(
                {"donwstream-header-mutation", per_route_config});
          }

          {
            // Per route header mutation for upstream.
            PerRouteProtoConfig header_mutation;
            auto response_mutation =
                header_mutation.mutable_mutations()->mutable_response_mutations()->Add();
            response_mutation->mutable_append()->mutable_header()->set_key(
                "upstream-per-route-flag-header");
            response_mutation->mutable_append()->mutable_header()->set_value(
                "upstream-per-route-flag-header-value");
            response_mutation->mutable_append()->set_append_action(
                envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
            ProtobufWkt::Any per_route_config;
            per_route_config.PackFrom(header_mutation);
            route->mutable_typed_per_filter_config()->insert(
                {"upstream-header-mutation", per_route_config});
          }

          {
            // Per route enable the filter that is disabled by default.
            envoy::config::route::v3::FilterConfig filter_config;
            filter_config.mutable_config()->PackFrom(PerRouteProtoConfig());
            filter_config.set_disabled(false);
            ProtobufWkt::Any per_route_config;
            per_route_config.PackFrom(filter_config);
            // Try enable the filter that is disabled by default.
            route->mutable_typed_per_filter_config()->insert(
                {"downstream-header-mutation-disabled-by-default", per_route_config});
          }

          {
            // Per route disable downstream header mutation.
            envoy::config::route::v3::FilterConfig filter_config;
            filter_config.set_disabled(true);
            ProtobufWkt::Any per_route_config;
            per_route_config.PackFrom(filter_config);
            another_route->mutable_typed_per_filter_config()->insert(
                {"donwstream-header-mutation", per_route_config});
            // Try disable upstream header mutation but this is not supported and should not work.
            another_route->mutable_typed_per_filter_config()->insert(
                {"upstream-header-mutation", per_route_config});
          }
        });
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HeaderMutationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(HeaderMutationIntegrationTest, TestHeaderMutation) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  EXPECT_EQ("downstream-request-global-flag-header-value",
            upstream_request_->headers()
                .get(Http::LowerCaseString("downstream-request-global-flag-header"))[0]
                ->value()
                .getStringView());
  EXPECT_EQ("downstream-request-global-flag-header-value-disabled-by-default",
            upstream_request_->headers()
                .get(Http::LowerCaseString(
                    "downstream-request-global-flag-header-disabled-by-default"))[0]
                ->value()
                .getStringView());
  EXPECT_EQ("upstream-request-global-flag-header-value",
            upstream_request_->headers()
                .get(Http::LowerCaseString("upstream-request-global-flag-header"))[0]
                ->value()
                .getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("downstream-global-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("downstream-global-flag-header"))[0]
                ->value()
                .getStringView());
  EXPECT_EQ("downstream-global-flag-header-value-disabled-by-default",
            response->headers()
                .get(Http::LowerCaseString("downstream-global-flag-header-disabled-by-default"))[0]
                ->value()
                .getStringView());
  EXPECT_EQ("downstream-per-route-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("downstream-per-route-flag-header"))[0]
                ->value()
                .getStringView());
  EXPECT_EQ("upstream-global-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("upstream-global-flag-header"))[0]
                ->value()
                .getStringView());
  EXPECT_EQ("upstream-per-route-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("upstream-per-route-flag-header"))[0]
                ->value()
                .getStringView());
  EXPECT_EQ("GET", response->headers()
                       .get(Http::LowerCaseString("request-method-in-upstream-filter"))[0]
                       ->value()
                       .getStringView());
  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest, TestDisableDownstreamHeaderMutation) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/disable/filter/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  EXPECT_EQ(0, upstream_request_->headers()
                   .get(Http::LowerCaseString("downstream-request-global-flag-header"))
                   .size());
  EXPECT_EQ(0, upstream_request_->headers()
                   .get(Http::LowerCaseString("downstream-request-global-flag-header-disabled-by-"
                                              "default"))
                   .size());

  EXPECT_EQ("upstream-request-global-flag-header-value",
            upstream_request_->headers()
                .get(Http::LowerCaseString("upstream-request-global-flag-header"))[0]
                ->value()
                .getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ(0,
            response->headers().get(Http::LowerCaseString("downstream-global-flag-header")).size());
  EXPECT_EQ(0, response->headers()
                   .get(Http::LowerCaseString("downstream-global-flag-header-disabled-by-default"))
                   .size());
  EXPECT_EQ(
      0, response->headers().get(Http::LowerCaseString("downstream-per-route-flag-header")).size());

  EXPECT_EQ("upstream-global-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("upstream-global-flag-header"))[0]
                ->value()
                .getStringView());
  EXPECT_EQ("GET", response->headers()
                       .get(Http::LowerCaseString("request-method-in-upstream-filter"))[0]
                       ->value()
                       .getStringView());
  codec_client_->close();
}

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
