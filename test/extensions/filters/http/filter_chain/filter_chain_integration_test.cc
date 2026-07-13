#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/extensions/filters/http/header_mutation/v3/header_mutation.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

class FilterChainIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  FilterChainIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, FilterChainIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test basic filter chain with header mutation filter
TEST_P(FilterChainIntegrationTest, BasicFilterChainWithHeaderMutation) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  default_filter_chain:
    filters:
    - name: envoy.filters.http.header_mutation
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
        mutations:
          response_mutations:
          - append:
              header:
                key: "x-new-header"
                value: "default-value"
              append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  config_helper_.prependFilter(filter_config, true);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(
      "default-value",
      response->headers().get(Http::LowerCaseString("x-new-header"))[0]->value().getStringView());
}

// Test empty filter chain (passthrough)
TEST_P(FilterChainIntegrationTest, EmptyFilterChainPassthrough) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
)EOF";

  config_helper_.prependFilter(filter_config, true);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test filter chain with per-route configuration using inline filter_chain
TEST_P(FilterChainIntegrationTest, PerRouteInlineFilterChain) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
)EOF";

  config_helper_.prependFilter(filter_config, true);

  // Add per-route config with inline filter chain
  config_helper_.addConfigModifier([](envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager& hcm) {
    auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
    auto* route = virtual_host->mutable_routes(0);
    auto* typed_per_filter_config = route->mutable_typed_per_filter_config();

    envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute per_route;
    auto* filter_chain = per_route.mutable_filter_chain();
    auto* filter = filter_chain->add_filters();
    filter->set_name("envoy.filters.http.header_mutation");
    envoy::extensions::filters::http::header_mutation::v3::HeaderMutation header_mutation_config;
    auto* mutation = header_mutation_config.mutable_mutations()->add_response_mutations();
    mutation->mutable_append()->set_append_action(
        envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
    mutation->mutable_append()->mutable_header()->set_key("x-new-header");
    mutation->mutable_append()->mutable_header()->set_value("value-from-inline-custom-chain");
    std::ignore = filter->mutable_typed_config()->PackFrom(header_mutation_config);

    std::ignore = (*typed_per_filter_config)["envoy.filters.http.filter_chain"].PackFrom(per_route);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(
      "value-from-inline-custom-chain",
      response->headers().get(Http::LowerCaseString("x-new-header"))[0]->value().getStringView());
}

// Adds a per-route filter chain config with a single header_mutation filter that appends
// `header_value` to `header_key`. `filter_name` controls the configured filter name, which is the
// key used for cross-chain override.
void addPerRouteHeaderMutation(ConfigHelper& config_helper, const std::string& filter_name,
                               const std::string& header_key, const std::string& header_value) {
  config_helper.addConfigModifier([filter_name, header_key, header_value](
                                      envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager& hcm) {
    auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
    auto* route = virtual_host->mutable_routes(0);
    auto* typed_per_filter_config = route->mutable_typed_per_filter_config();

    envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute per_route;
    auto* filter_chain = per_route.mutable_filter_chain();
    auto* filter = filter_chain->add_filters();
    filter->set_name(filter_name);
    envoy::extensions::filters::http::header_mutation::v3::HeaderMutation header_mutation_config;
    auto* mutation = header_mutation_config.mutable_mutations()->add_response_mutations();
    mutation->mutable_append()->set_append_action(
        envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
    mutation->mutable_append()->mutable_header()->set_key(header_key);
    mutation->mutable_append()->mutable_header()->set_value(header_value);
    std::ignore = filter->mutable_typed_config()->PackFrom(header_mutation_config);

    std::ignore = (*typed_per_filter_config)["envoy.filters.http.filter_chain"].PackFrom(per_route);
  });
}

// The default filter chain and a per-route filter chain with distinct filter names are merged, so
// both filters run.
TEST_P(FilterChainIntegrationTest, DefaultAndPerRouteMerged) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  default_filter_chain:
    filters:
    - name: header_mutation_default
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
        mutations:
          response_mutations:
          - append:
              header:
                key: "x-default-header"
                value: "from-default"
              append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  config_helper_.prependFilter(filter_config, true);
  addPerRouteHeaderMutation(config_helper_, "header_mutation_route", "x-route-header",
                            "from-route");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("from-default", response->headers()
                                .get(Http::LowerCaseString("x-default-header"))[0]
                                ->value()
                                .getStringView());
  EXPECT_EQ(
      "from-route",
      response->headers().get(Http::LowerCaseString("x-route-header"))[0]->value().getStringView());
}

// When the default chain and the per-route chain configure a filter with the same name, the more
// specific per-route chain overrides the default: only the per-route filter runs.
TEST_P(FilterChainIntegrationTest, PerRouteOverridesDefault) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  default_filter_chain:
    filters:
    - name: envoy.filters.http.header_mutation
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
        mutations:
          response_mutations:
          - append:
              header:
                key: "x-shared-header"
                value: "from-default"
              append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  config_helper_.prependFilter(filter_config, true);
  addPerRouteHeaderMutation(config_helper_, "envoy.filters.http.header_mutation", "x-shared-header",
                            "from-route");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // The default chain's same-named filter is overridden, so only "from-route" is appended.
  const auto values = response->headers().get(Http::LowerCaseString("x-shared-header"));
  ASSERT_EQ(1, values.size());
  EXPECT_EQ("from-route", values[0]->value().getStringView());
}

} // namespace
} // namespace Envoy
