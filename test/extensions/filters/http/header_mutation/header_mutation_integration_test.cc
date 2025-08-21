#include <bitset>

#include "source/extensions/filters/http/header_mutation/config.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {
namespace {

enum RouteLevel {
  PerRoute = 0,
  VirtualHost = 1,
  RouteTable = 2,
};
using RouteLevelFlag = std::bitset<3>;

RouteLevelFlag PerRouteLevel = {1 << RouteLevel::PerRoute};
RouteLevelFlag VirtualHostLevel = {1 << RouteLevel::VirtualHost};
RouteLevelFlag RouteTableLevel = {1 << RouteLevel::RouteTable};
RouteLevelFlag AllRoutesLevel = {PerRouteLevel | VirtualHostLevel | RouteTableLevel};

class HeaderMutationIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
  std::string upstream_header_mutation_config_{R"EOF(
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
)EOF"};

public:
  HeaderMutationIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void setPerFilterConfigsWithSameKey(const std::string& prefix) {
    config_helper_.addConfigModifier(
        [prefix](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          auto* another_route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_routes();
          *another_route = *route;

          route->mutable_match()->set_path("/default/route");
          another_route->mutable_match()->set_path("/disable/filter/route");

          // Disable downstream-header-mutation filter
          if (prefix == "upstream") {
            // Per route disable downstream header mutation.
            envoy::config::route::v3::FilterConfig filter_config;
            filter_config.set_disabled(true);
            Protobuf::Any per_route_config;
            per_route_config.PackFrom(filter_config);
            another_route->mutable_typed_per_filter_config()->insert(
                {"downstream-header-mutation", per_route_config});
          }

          // Per route header mutation.
          PerRouteProtoConfig header_mutation;
          auto response_mutation =
              header_mutation.mutable_mutations()->mutable_response_mutations()->Add();
          response_mutation->mutable_append()->mutable_header()->set_key(
              absl::StrCat(prefix, "-flag-header"));
          response_mutation->mutable_append()->mutable_header()->set_value(
              absl::StrCat(prefix, "-per-route-flag-header-value"));
          response_mutation->mutable_append()->set_append_action(
              envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);

          Protobuf::Any per_route_config;
          per_route_config.PackFrom(header_mutation);
          route->mutable_typed_per_filter_config()->insert(
              {absl::StrCat(prefix, "-header-mutation"), per_route_config});

          // Set response mutations on other levels than per route with same key. This is to test
          // per filter config specificity order of downstream filter.
          if (prefix == "downstream") {
            // Per virtual host header mutation.
            PerRouteProtoConfig header_mutation_vhost;
            auto response_mutation_vhost =
                header_mutation_vhost.mutable_mutations()->mutable_response_mutations()->Add();
            response_mutation_vhost->mutable_append()->mutable_header()->set_key(
                absl::StrCat(prefix, "-flag-header"));
            response_mutation_vhost->mutable_append()->mutable_header()->set_value(
                absl::StrCat(prefix, "-per-vHost-flag-header-value"));
            response_mutation_vhost->mutable_append()->set_append_action(
                envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);

            Protobuf::Any per_route_config_vhost;
            per_route_config_vhost.PackFrom(header_mutation_vhost);

            auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts(0);
            vhost->mutable_typed_per_filter_config()->insert(
                {absl::StrCat(prefix, "-header-mutation"), per_route_config_vhost});

            // Per route table header mutation.
            PerRouteProtoConfig header_mutation_rt;
            auto response_mutation_rt =
                header_mutation_rt.mutable_mutations()->mutable_response_mutations()->Add();
            response_mutation_rt->mutable_append()->mutable_header()->set_key(
                absl::StrCat(prefix, "-flag-header"));
            response_mutation_rt->mutable_append()->mutable_header()->set_value(
                absl::StrCat(prefix, "-route-table-flag-header-value"));
            response_mutation_rt->mutable_append()->set_append_action(
                envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);

            Protobuf::Any per_route_config_rt;
            per_route_config_rt.PackFrom(header_mutation_rt);

            auto* route_table = hcm.mutable_route_config();
            route_table->mutable_typed_per_filter_config()->insert(
                {absl::StrCat(prefix, "-header-mutation"), per_route_config_rt});
          }
        });
  }

  void initializeFilterForSpecificityTest(bool most_specific_header_mutations_wins,
                                          bool disable_downstream_header_mutation = false) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
    // Add `downstream-header-mutation` filter to the filter chain.
    envoy::extensions::filters::http::header_mutation::v3::HeaderMutation header_mutation;
    std::string header_mutation_config = R"EOF(
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
)EOF";
    TestUtility::loadFromYaml(header_mutation_config, header_mutation);
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter
        http_mutation_filter;
    http_mutation_filter.set_name("downstream-header-mutation");
    if (most_specific_header_mutations_wins) {
      header_mutation.set_most_specific_header_mutations_wins(true);
    }
    http_mutation_filter.mutable_typed_config()->PackFrom(header_mutation);

    config_helper_.prependFilter(
        MessageUtil::getJsonStringFromMessageOrError(http_mutation_filter));

    if (!disable_downstream_header_mutation) {
      // Set per filter configs for `downstream-header-mutation` filter.
      setPerFilterConfigsWithSameKey("downstream");
    } else {
      // Add `upstream-header-mutation` filter.
      TestUtility::loadFromYaml(upstream_header_mutation_config_, header_mutation);
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter
          http_mutation_filter;
      http_mutation_filter.set_name("upstream-header-mutation");
      if (most_specific_header_mutations_wins) {
        header_mutation.set_most_specific_header_mutations_wins(true);
      }
      http_mutation_filter.mutable_typed_config()->PackFrom(header_mutation);
      config_helper_.prependFilter(
          MessageUtil::getJsonStringFromMessageOrError(http_mutation_filter), false);
      // Set per filter configs for `upstream-header-mutation` filter and disable downstream filter.
      setPerFilterConfigsWithSameKey("upstream");
    }

    HttpIntegrationTest::initialize();
  }

  // Adds the header mutation filter to the upstream filter chain to test enabling the filter by
  // default while disabling at the route level, and disabling the filter by default while enabling
  // at the route level. For example, if disabled_at_route_level is true, then the filter will be
  // enabled by default, and disabled by per-route configuration.
  void
  initializeUpstreamFilterForSpecificityEnableDisableTest(bool most_specific_header_mutations_wins,
                                                          RouteLevelFlag route_level,
                                                          bool disabled_at_route_level) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
    envoy::extensions::filters::http::header_mutation::v3::HeaderMutation header_mutation;
    TestUtility::loadFromYaml(upstream_header_mutation_config_, header_mutation);
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter
        http_mutation_filter;
    http_mutation_filter.set_name("upstream-header-mutation");
    // We are testing enabled from various level, so disable the filter by default
    http_mutation_filter.set_disabled(!disabled_at_route_level);
    header_mutation.set_most_specific_header_mutations_wins(most_specific_header_mutations_wins);
    http_mutation_filter.mutable_typed_config()->PackFrom(header_mutation);
    config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(http_mutation_filter),
                                 false);

    config_helper_.addConfigModifier(
        [route_level, disabled_at_route_level](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          route->mutable_match()->set_path("/default/route");

          // Per route header mutation.
          envoy::config::route::v3::FilterConfig per_route_filter_config;
          if (route_level.test(RouteLevel::PerRoute)) {
            per_route_filter_config.set_disabled(disabled_at_route_level);
            PerRouteProtoConfig header_mutation;
            per_route_filter_config.mutable_config()->PackFrom(header_mutation);
            Protobuf::Any per_route_config;
            per_route_config.PackFrom(per_route_filter_config);
            route->mutable_typed_per_filter_config()->insert(
                {"upstream-header-mutation", per_route_config});
          }

          // Per virtual host header mutation.
          envoy::config::route::v3::FilterConfig per_route_filter_config_vhost;
          if (route_level.test(RouteLevel::VirtualHost)) {
            per_route_filter_config_vhost.set_disabled(disabled_at_route_level);
            PerRouteProtoConfig header_mutation_vhost;
            per_route_filter_config_vhost.mutable_config()->PackFrom(header_mutation_vhost);
            Protobuf::Any per_route_config_vhost;
            per_route_config_vhost.PackFrom(per_route_filter_config_vhost);

            auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts(0);
            vhost->mutable_typed_per_filter_config()->insert(
                {"upstream-header-mutation", per_route_config_vhost});
          }

          // Per route table header mutation.
          envoy::config::route::v3::FilterConfig per_route_filter_config_rt;
          if (route_level.test(RouteLevel::RouteTable)) {
            per_route_filter_config_rt.set_disabled(disabled_at_route_level);
            PerRouteProtoConfig header_mutation_rt;
            per_route_filter_config_rt.mutable_config()->PackFrom(header_mutation_rt);
            Protobuf::Any per_route_config_rt;
            per_route_config_rt.PackFrom(per_route_filter_config_rt);

            auto* route_table = hcm.mutable_route_config();
            route_table->mutable_typed_per_filter_config()->insert(
                {"upstream-header-mutation", per_route_config_rt});
          }
        });
    HttpIntegrationTest::initialize();
  }

  void initializeFilter(RouteLevelFlag route_level) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);

    config_helper_.prependFilter(R"EOF(
name: downstream-header-mutation
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
    config_helper_.prependFilter(R"EOF(
name: upstream-header-mutation-disabled-by-default
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
  mutations:
    request_mutations:
    - append:
        header:
          key: "upstream-request-global-flag-header-disabled-by-default"
          value: "upstream-request-global-flag-header-value-disabled-by-default"
        append_action: APPEND_IF_EXISTS_OR_ADD
    response_mutations:
    - append:
        header:
          key: "upstream-global-flag-header-disabled-by-default"
          value: "upstream-global-flag-header-value-disabled-by-default"
        append_action: APPEND_IF_EXISTS_OR_ADD
    - append:
        header:
          key: "request-method-in-upstream-filter-disabled-by-default"
          value: "%REQ(:METHOD)%"
        append_action: APPEND_IF_EXISTS_OR_ADD
disabled: true
)EOF",
                                 false);

    config_helper_.addConfigModifier(
        [route_level](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);

          // Another route that disables downstream header mutation.
          auto* another_route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_routes();
          *another_route = *route;

          route->mutable_match()->set_path("/default/route");
          another_route->mutable_match()->set_path("/disable/filter/route");

          if (route_level.test(RouteLevel::PerRoute)) {
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

              auto request_mutation =
                  header_mutation.mutable_mutations()->mutable_request_mutations()->Add();
              request_mutation->mutable_append()->mutable_header()->set_key(
                  "downstream-request-per-route-flag-header");
              request_mutation->mutable_append()->mutable_header()->set_value(
                  "downstream-request-per-route-flag-header-value");
              request_mutation->mutable_append()->set_append_action(
                  envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

              Protobuf::Any per_route_config;
              per_route_config.PackFrom(header_mutation);
              route->mutable_typed_per_filter_config()->insert(
                  {"downstream-header-mutation", per_route_config});
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
              Protobuf::Any per_route_config;
              per_route_config.PackFrom(header_mutation);
              route->mutable_typed_per_filter_config()->insert(
                  {"upstream-header-mutation", per_route_config});
            }

            {
              // Per route enable the filter that is disabled by default.
              envoy::config::route::v3::FilterConfig filter_config;
              filter_config.mutable_config()->PackFrom(PerRouteProtoConfig());
              filter_config.set_disabled(false);
              Protobuf::Any per_route_config;
              per_route_config.PackFrom(filter_config);
              // Try enable the filter that is disabled by default.
              route->mutable_typed_per_filter_config()->insert(
                  {"downstream-header-mutation-disabled-by-default", per_route_config});
              route->mutable_typed_per_filter_config()->insert(
                  {"upstream-header-mutation-disabled-by-default", per_route_config});
            }

            {
              // Per route disable downstream and upstream header mutation.
              envoy::config::route::v3::FilterConfig filter_config;
              filter_config.set_disabled(true);
              Protobuf::Any per_route_config;
              per_route_config.PackFrom(filter_config);
              another_route->mutable_typed_per_filter_config()->insert(
                  {"downstream-header-mutation", per_route_config});
              another_route->mutable_typed_per_filter_config()->insert(
                  {"upstream-header-mutation", per_route_config});
            }
          }

          if (route_level.test(RouteLevel::VirtualHost)) {
            {
              // Per virtual host header mutation for downstream.
              PerRouteProtoConfig header_mutation_vhost;
              auto response_mutation_vhost =
                  header_mutation_vhost.mutable_mutations()->mutable_response_mutations()->Add();
              response_mutation_vhost->mutable_append()->mutable_header()->set_key(
                  "downstream-per-vHost-flag-header");
              response_mutation_vhost->mutable_append()->mutable_header()->set_value(
                  "downstream-per-vHost-flag-header-value");
              response_mutation_vhost->mutable_append()->set_append_action(
                  envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

              Protobuf::Any per_route_config_vhost;
              per_route_config_vhost.PackFrom(header_mutation_vhost);

              auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts(0);
              vhost->mutable_typed_per_filter_config()->insert(
                  {"downstream-header-mutation", per_route_config_vhost});
            }
            {
              // Per virtual host header mutation for upstream.
              PerRouteProtoConfig header_mutation_vhost;
              auto response_mutation_vhost =
                  header_mutation_vhost.mutable_mutations()->mutable_response_mutations()->Add();
              response_mutation_vhost->mutable_append()->mutable_header()->set_key(
                  "upstream-per-vHost-flag-header");
              response_mutation_vhost->mutable_append()->mutable_header()->set_value(
                  "upstream-per-vHost-flag-header-value");
              response_mutation_vhost->mutable_append()->set_append_action(
                  envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

              Protobuf::Any per_route_config_vhost;
              per_route_config_vhost.PackFrom(header_mutation_vhost);

              auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts(0);
              vhost->mutable_typed_per_filter_config()->insert(
                  {"upstream-header-mutation", per_route_config_vhost});
            }
          }

          if (route_level.test(RouteLevel::RouteTable)) {
            {
              // Per route table header mutation for downstream.
              PerRouteProtoConfig header_mutation_rt;
              auto response_mutation_rt =
                  header_mutation_rt.mutable_mutations()->mutable_response_mutations()->Add();
              response_mutation_rt->mutable_append()->mutable_header()->set_key(
                  "downstream-route-table-flag-header");
              response_mutation_rt->mutable_append()->mutable_header()->set_value(
                  "downstream-route-table-flag-header-value");
              response_mutation_rt->mutable_append()->set_append_action(
                  envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

              Protobuf::Any per_route_config_rt;
              per_route_config_rt.PackFrom(header_mutation_rt);

              auto* route_table = hcm.mutable_route_config();
              route_table->mutable_typed_per_filter_config()->insert(
                  {"downstream-header-mutation", per_route_config_rt});
            }
            {
              // Per route table header mutation for upstream.
              PerRouteProtoConfig header_mutation_rt;
              auto response_mutation_rt =
                  header_mutation_rt.mutable_mutations()->mutable_response_mutations()->Add();
              response_mutation_rt->mutable_append()->mutable_header()->set_key(
                  "upstream-route-table-flag-header");
              response_mutation_rt->mutable_append()->mutable_header()->set_value(
                  "upstream-route-table-flag-header-value");
              response_mutation_rt->mutable_append()->set_append_action(
                  envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

              Protobuf::Any per_route_config_rt;
              per_route_config_rt.PackFrom(header_mutation_rt);

              auto* route_table = hcm.mutable_route_config();
              route_table->mutable_typed_per_filter_config()->insert(
                  {"upstream-header-mutation", per_route_config_rt});
            }
          }
        });
    HttpIntegrationTest::initialize();
  }

  void checkHeader(int line_num, IntegrationStreamDecoder& response, absl::string_view key,
                   bool exists, absl::string_view value = "") {
    SCOPED_TRACE(line_num);
    auto headers = response.headers().get(Http::LowerCaseString(key));
    if (exists) {
      EXPECT_EQ(headers.size(), 1);
      if (value.empty()) {
        EXPECT_EQ(headers[0]->value().getStringView(), absl::StrCat(key, "-value"));
      } else {
        EXPECT_EQ(headers[0]->value().getStringView(), value);
      }
    } else {
      EXPECT_EQ(headers.size(), 0);
    }
  };

  void checkHeader(int line_num, FakeStream& request, absl::string_view key, bool exists) {
    SCOPED_TRACE(line_num);
    auto headers = request.headers().get(Http::LowerCaseString(key));
    if (exists) {
      EXPECT_EQ(headers.size(), 1);
      EXPECT_EQ(headers[0]->value().getStringView(), absl::StrCat(key, "-value"));
    } else {
      EXPECT_EQ(headers.size(), 0);
    }
  };
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HeaderMutationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

void testResponseHeaderMutation(IntegrationStreamDecoder* response, RouteLevelFlag route_level) {
  if (route_level.test(RouteLevel::PerRoute)) {
    EXPECT_EQ("downstream-per-route-flag-header-value",
              response->headers()
                  .get(Http::LowerCaseString("downstream-per-route-flag-header"))[0]
                  ->value()
                  .getStringView());
    EXPECT_EQ("upstream-per-route-flag-header-value",
              response->headers()
                  .get(Http::LowerCaseString("upstream-per-route-flag-header"))[0]
                  ->value()
                  .getStringView());
  } else {
    EXPECT_EQ(
        response->headers().get(Http::LowerCaseString("downstream-per-route-flag-header")).size(),
        0);
    EXPECT_EQ(
        response->headers().get(Http::LowerCaseString("upstream-per-route-flag-header")).size(), 0);
  }

  if (route_level.test(RouteLevel::VirtualHost)) {
    EXPECT_EQ("downstream-per-vHost-flag-header-value",
              response->headers()
                  .get(Http::LowerCaseString("downstream-per-vHost-flag-header"))[0]
                  ->value()
                  .getStringView());
    EXPECT_EQ("upstream-per-vHost-flag-header-value",
              response->headers()
                  .get(Http::LowerCaseString("upstream-per-vHost-flag-header"))[0]
                  ->value()
                  .getStringView());
  } else {
    EXPECT_EQ(
        response->headers().get(Http::LowerCaseString("downstream-per-vHost-flag-header")).size(),
        0);
    EXPECT_EQ(
        response->headers().get(Http::LowerCaseString("upstream-per-vHost-flag-header")).size(), 0);
  }

  if (route_level.test(RouteLevel::RouteTable)) {
    EXPECT_EQ("downstream-route-table-flag-header-value",
              response->headers()
                  .get(Http::LowerCaseString("downstream-route-table-flag-header"))[0]
                  ->value()
                  .getStringView());
    EXPECT_EQ("upstream-route-table-flag-header-value",
              response->headers()
                  .get(Http::LowerCaseString("upstream-route-table-flag-header"))[0]
                  ->value()
                  .getStringView());
  } else {
    EXPECT_EQ(
        response->headers().get(Http::LowerCaseString("downstream-route-table-flag-header")).size(),
        0);
    EXPECT_EQ(
        response->headers().get(Http::LowerCaseString("upstream-route-table-flag-header")).size(),
        0);
  }
}

TEST_P(HeaderMutationIntegrationTest, TestHeaderMutationAllLevelsApplied) {
  initializeFilter(AllRoutesLevel);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  EXPECT_EQ("downstream-request-global-flag-header-value",
            upstream_request_->headers()
                .get(Http::LowerCaseString("downstream-request-global-flag-header"))[0]
                ->value()
                .getStringView());

  EXPECT_EQ("downstream-request-per-route-flag-header-value",
            upstream_request_->headers()
                .get(Http::LowerCaseString("downstream-request-per-route-flag-header"))[0]
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

  // This header is injected by the "upstream-header-mutation-disabled-by-default" upstream filter
  // which is disabled by default and re-enabled at route level at /default/route path
  EXPECT_EQ(
      upstream_request_->headers()
          .get(Http::LowerCaseString("upstream-request-global-flag-header-disabled-by-default"))[0]
          ->value()
          .getStringView(),
      "upstream-request-global-flag-header-value-disabled-by-default");

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

  EXPECT_EQ("upstream-global-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("upstream-global-flag-header"))[0]
                ->value()
                .getStringView());

  // These two headers are injected by the "upstream-header-mutation-disabled-by-default" upstream
  // filter which is disabled by default and re-enabled at route level at /default/route path
  EXPECT_EQ(response->headers()
                .get(Http::LowerCaseString("upstream-global-flag-header-disabled-by-default"))[0]
                ->value()
                .getStringView(),
            "upstream-global-flag-header-value-disabled-by-default");
  EXPECT_EQ(
      response->headers()
          .get(Http::LowerCaseString("request-method-in-upstream-filter-disabled-by-default"))[0]
          ->value()
          .getStringView(),
      "GET");

  testResponseHeaderMutation(response.get(), AllRoutesLevel);

  EXPECT_EQ("GET", response->headers()
                       .get(Http::LowerCaseString("request-method-in-upstream-filter"))[0]
                       ->value()
                       .getStringView());
  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest, TestHeaderMutationMostSpecificWins) {
  initializeFilterForSpecificityTest(/*most_specific_header_mutations_wins=*/true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
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

  // Expect the most specific level (route level) header mutation is set.
  EXPECT_EQ("downstream-per-route-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("downstream-flag-header"))[0]
                ->value()
                .getStringView());
  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest, TestHeaderMutationLeastSpecificWins) {
  initializeFilterForSpecificityTest(/*most_specific_header_mutations_wins=*/false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
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

  // Expect the least specific level (route table level) header mutation is set.
  EXPECT_EQ("downstream-route-table-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("downstream-flag-header"))[0]
                ->value()
                .getStringView());
  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest, TestHeaderMutationPerRoute) {
  initializeFilter(PerRouteLevel);

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

  EXPECT_EQ("upstream-global-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("upstream-global-flag-header"))[0]
                ->value()
                .getStringView());

  testResponseHeaderMutation(response.get(), PerRouteLevel);

  EXPECT_EQ("GET", response->headers()
                       .get(Http::LowerCaseString("request-method-in-upstream-filter"))[0]
                       ->value()
                       .getStringView());
  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest, TestHeaderMutationPerVirtualHost) {
  initializeFilter(VirtualHostLevel);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  EXPECT_EQ("downstream-request-global-flag-header-value",
            upstream_request_->headers()
                .get(Http::LowerCaseString("downstream-request-global-flag-header"))[0]
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

  EXPECT_EQ("upstream-global-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("upstream-global-flag-header"))[0]
                ->value()
                .getStringView());
  testResponseHeaderMutation(response.get(), VirtualHostLevel);

  EXPECT_EQ("GET", response->headers()
                       .get(Http::LowerCaseString("request-method-in-upstream-filter"))[0]
                       ->value()
                       .getStringView());
  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest, TestHeaderMutationPerRouteTable) {
  initializeFilter(RouteTableLevel);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  EXPECT_EQ("downstream-request-global-flag-header-value",
            upstream_request_->headers()
                .get(Http::LowerCaseString("downstream-request-global-flag-header"))[0]
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

  EXPECT_EQ("upstream-global-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("upstream-global-flag-header"))[0]
                ->value()
                .getStringView());

  testResponseHeaderMutation(response.get(), RouteTableLevel);

  EXPECT_EQ("GET", response->headers()
                       .get(Http::LowerCaseString("request-method-in-upstream-filter"))[0]
                       ->value()
                       .getStringView());
  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest, TestPerRouteDisableDownstreamAndUpstreamHeaderMutation) {
  initializeFilter(AllRoutesLevel);
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

  EXPECT_EQ(0, upstream_request_->headers()
                   .get(Http::LowerCaseString("upstream-request-global-flag-header"))
                   .size());

  EXPECT_EQ(
      0, upstream_request_->headers()
             .get(Http::LowerCaseString("upstream-request-global-flag-header-disabled-by-default"))
             .size());

  EXPECT_EQ(upstream_request_->headers()
                .get(Http::LowerCaseString("downstream-request-per-route-flag-header"))
                .size(),
            0);

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
  EXPECT_EQ(
      response->headers().get(Http::LowerCaseString("downstream-per-vHost-flag-header")).size(), 0);

  EXPECT_EQ(
      response->headers().get(Http::LowerCaseString("downstream-route-table-flag-header")).size(),
      0);

  EXPECT_EQ(0,
            response->headers().get(Http::LowerCaseString("upstream-global-flag-header")).size());

  EXPECT_EQ(0, response->headers()
                   .get(Http::LowerCaseString("upstream-global-flag-header-disabled-by-default"))
                   .size());

  EXPECT_EQ(0,
            response->headers()
                .get(Http::LowerCaseString("request-method-in-upstream-filter-disabled-by-default"))
                .size());

  EXPECT_EQ(
      0,
      response->headers().get(Http::LowerCaseString("request-method-in-upstream-filter")).size());

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest, TestDisableDownstreamHeaderMutationWithSpecific) {
  initializeFilterForSpecificityTest(/*most_specific_header_mutations_wins=*/false,
                                     /*disable_downstream_header_mutation=*/true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/disable/filter/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  EXPECT_EQ(0, upstream_request_->headers()
                   .get(Http::LowerCaseString("downstream-request-global-flag-header"))
                   .size());

  // This header was never set in the config
  EXPECT_EQ(0, upstream_request_->headers()
                   .get(Http::LowerCaseString("downstream-request-global-flag-header-disabled-by-"
                                              "default"))
                   .size());

  EXPECT_EQ("upstream-request-global-flag-header-value",
            upstream_request_->headers()
                .get(Http::LowerCaseString("upstream-request-global-flag-header"))[0]
                ->value()
                .getStringView());

  // This header was never set in the config
  EXPECT_EQ(upstream_request_->headers()
                .get(Http::LowerCaseString("downstream-request-per-route-flag-header"))
                .size(),
            0);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ(0,
            response->headers().get(Http::LowerCaseString("downstream-global-flag-header")).size());

  EXPECT_EQ("upstream-global-flag-header-value",
            response->headers()
                .get(Http::LowerCaseString("upstream-global-flag-header"))[0]
                ->value()
                .getStringView());
  EXPECT_EQ("GET", response->headers()
                       .get(Http::LowerCaseString("request-method-in-upstream-filter"))[0]
                       ->value()
                       .getStringView());
  EXPECT_EQ(0, response->headers().get(Http::LowerCaseString("upstream-flag-header")).size());
  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestDisableUpstreamHeaderMutationAtRouteTableLevelMostSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(true, RouteTableLevel, true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", false);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", false);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", false);

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestDisableUpstreamHeaderMutationAtRouteTableLevelLeastSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(false, RouteTableLevel, true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", false);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", false);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", false);

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestDisableUpstreamHeaderMutationAtVHostLevelMostSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(true, VirtualHostLevel, true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", false);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", false);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", false);

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestDisableUpstreamHeaderMutationAtVHostLevelLeastSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(false, VirtualHostLevel, true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", false);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", false);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", false);

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestDisableUpstreamHeaderMutationAtPerRouteLevelMostSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(true, PerRouteLevel, true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", false);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", false);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", false);

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestDisableUpstreamHeaderMutationAtPerRouteLevelLeastSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(false, PerRouteLevel, true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", false);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", false);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", false);

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestEnableUpstreamHeaderMutationAtPerRouteLevelLeastSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(false, PerRouteLevel, false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", true);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", true);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", true, "GET");

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestEnableUpstreamHeaderMutationAtPerRouteLevelMostSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(true, PerRouteLevel, false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", true);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", true);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", true, "GET");

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestEnableUpstreamHeaderMutationAtVHostLevelLeastSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(false, VirtualHostLevel, false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", true);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", true);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", true, "GET");

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestEnableUpstreamHeaderMutationAtVHostLevelMostSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(true, VirtualHostLevel, false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", true);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", true);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", true, "GET");

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestEnableUpstreamHeaderMutationAtRouteTableLevelLeastSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(false, RouteTableLevel, false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", true);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", true);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", true, "GET");

  codec_client_->close();
}

TEST_P(HeaderMutationIntegrationTest,
       TestEnableUpstreamHeaderMutationAtRouteTableLevelMostSpecificWins) {
  initializeUpstreamFilterForSpecificityEnableDisableTest(true, RouteTableLevel, false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  checkHeader(__LINE__, *upstream_request_, "upstream-request-global-flag-header", true);

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  checkHeader(__LINE__, *response, "upstream-global-flag-header", true);
  checkHeader(__LINE__, *response, "request-method-in-upstream-filter", true, "GET");

  codec_client_->close();
}
} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
