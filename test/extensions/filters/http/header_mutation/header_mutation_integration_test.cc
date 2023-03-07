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
    config_helper_.addFilter(R"EOF(
name: donwstream-header-mutation
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
  mutations:
    response_mutations:
    - append:
        header:
          key: "downstream-global-flag-header"
          value: "downstream-global-flag-header-value"
        append_action: APPEND_IF_EXISTS_OR_ADD
)EOF");

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::extensions::upstreams::http::v3::HttpProtocolOptions http_protocol_options;
      http_protocol_options.mutable_use_downstream_protocol_config();

      // Upstream header mutation filter.
      auto filter_0 = http_protocol_options.mutable_http_filters()->Add();
      filter_0->set_name("upstream-header-mutation");
      ProtoConfig header_mutation;
      auto response_mutation =
          header_mutation.mutable_mutations()->mutable_response_mutations()->Add();
      response_mutation->mutable_append()->mutable_header()->set_key("upstream-global-flag-header");
      response_mutation->mutable_append()->mutable_header()->set_value(
          "upstream-global-flag-header-value");
      response_mutation->mutable_append()->set_append_action(
          envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
      // Add a second response mutation to test the request headers could be obtained in the
      // upstream filter.
      response_mutation = header_mutation.mutable_mutations()->mutable_response_mutations()->Add();
      response_mutation->mutable_append()->mutable_header()->set_key(
          "request-method-in-upstream-filter");
      response_mutation->mutable_append()->mutable_header()->set_value("%REQ(:METHOD)%");
      response_mutation->mutable_append()->set_append_action(
          envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

      filter_0->mutable_typed_config()->PackFrom(header_mutation);

      // Upstream codec filter.
      auto* codec_filter = http_protocol_options.mutable_http_filters()->Add();
      codec_filter->set_name("envoy.filters.http.upstream_codec");
      codec_filter->mutable_typed_config()->PackFrom(
          envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec::default_instance());

      ProtobufWkt::Any any_protocol_options;
      any_protocol_options.PackFrom(http_protocol_options);

      bootstrap.mutable_static_resources()
          ->mutable_clusters(0)
          ->mutable_typed_extension_protocol_options()
          ->insert(
              {"envoy.extensions.upstreams.http.v3.HttpProtocolOptions", any_protocol_options});
    });

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);

          // Per route header mutation for downstream.
          PerRouteProtoConfig header_mutation_1;
          auto response_mutation =
              header_mutation_1.mutable_mutations()->mutable_response_mutations()->Add();
          response_mutation->mutable_append()->mutable_header()->set_key(
              "downstream-per-route-flag-header");
          response_mutation->mutable_append()->mutable_header()->set_value(
              "downstream-per-route-flag-header-value");
          response_mutation->mutable_append()->set_append_action(
              envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

          ProtobufWkt::Any per_route_config_1;
          per_route_config_1.PackFrom(header_mutation_1);

          route->mutable_typed_per_filter_config()->insert(
              {"donwstream-header-mutation", per_route_config_1});

          // Per route header mutation for upstream.
          PerRouteProtoConfig header_mutation_2;
          response_mutation =
              header_mutation_2.mutable_mutations()->mutable_response_mutations()->Add();
          response_mutation->mutable_append()->mutable_header()->set_key(
              "upstream-per-route-flag-header");
          response_mutation->mutable_append()->mutable_header()->set_value(
              "upstream-per-route-flag-header-value");
          response_mutation->mutable_append()->set_append_action(
              envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

          ProtobufWkt::Any per_route_config_2;
          per_route_config_2.PackFrom(header_mutation_2);

          route->mutable_typed_per_filter_config()->insert(
              {"upstream-header-mutation", per_route_config_2});
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

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("downstream-global-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("downstream-global-flag-header"))[0]
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

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
