#include <cstdint>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/admin/v3/config_dump_shared.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/api_version.h"
#include "source/common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/scoped_rds.h"
#include "test/integration/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAre;
using testing::Eq;
using testing::Ge;
using testing::HasSubstr;
namespace Envoy {
namespace {

class InlineScopedRoutesIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  InlineScopedRoutesIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    skip_tag_extraction_rule_check_ = false;
  }

  void setScopedRoutesConfig(absl::string_view config_yaml) {
    config_helper_.addConfigModifier(
        [config_yaml](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes*
              scoped_routes = hcm.mutable_scoped_routes();
          const std::string scoped_routes_yaml = absl::StrCat(R"EOF(
name: foo-scoped-routes
scope_key_builder:
  fragments:
    - header_value_extractor:
        name: Addr
        element_separator: ;
        element:
          key: x-foo-key
          separator: =
)EOF",
                                                              config_yaml);
          TestUtility::loadFromYaml(scoped_routes_yaml, *scoped_routes);
        });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, InlineScopedRoutesIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(InlineScopedRoutesIntegrationTest, NoScopeFound) {
  absl::string_view config_yaml = R"EOF(
scoped_route_configurations_list:
  scoped_route_configurations:
    - name: foo-scope
      route_configuration:
        name: foo
        virtual_hosts:
          - name: bar
            domains: ["*"]
            routes:
              - match: { prefix: "/" }
                route: { cluster: cluster_0 }
      key:
        fragments: { string_key: foo }
)EOF";
  setScopedRoutesConfig(config_yaml);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     // "xyz-route" is not a configured scope key.
                                     {"Addr", "x-foo-key=xyz-route"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();
}

TEST_P(InlineScopedRoutesIntegrationTest, ScopeWithSingleRouteConfiguration) {
  absl::string_view config_yaml = R"EOF(
scoped_route_configurations_list:
  scoped_route_configurations:
    - name: foo-scope
      route_configuration:
        name: foo
        virtual_hosts:
          - name: bar
            domains: ["*"]
            routes:
              - match: { prefix: "/" }
                route: { cluster: cluster_0 }
      key:
        fragments: { string_key: foo }
)EOF";
  setScopedRoutesConfig(config_yaml);
  initialize();

  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo"}},
      /*request_size=*/0, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "foo"}},
      /*response_size=*/0,
      /*backend_idx=*/0);
}

TEST_P(InlineScopedRoutesIntegrationTest, ScopeWithMultipleRouteConfigurations) {
  absl::string_view config_yaml = R"EOF(
scoped_route_configurations_list:
  scoped_route_configurations:
    - name: foo-scope
      route_configuration:
        name: foo
        virtual_hosts:
          - name: bar
            domains: ["*"]
            routes:
              - match: { prefix: "/" }
                route: { cluster: cluster_0 }
            response_headers_to_add:
              header: { key: route-name, value: foo }
      key:
        fragments: { string_key: foo }
    - name: baz-scope
      route_configuration:
        name: baz
        virtual_hosts:
          - name: bar
            domains: ["*"]
            routes:
              - match: { prefix: "/" }
                route: { cluster: cluster_0 }
            response_headers_to_add:
              header: { key: route-name, value: baz }
      key:
        fragments: { string_key: baz }

)EOF";
  setScopedRoutesConfig(config_yaml);
  initialize();

  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=baz"}},
      /*request_size=*/0, Http::TestResponseHeaderMapImpl{{":status", "200"}},
      /*response_size=*/0,
      /*backend_idx=*/0, Http::TestResponseHeaderMapImpl{{"route-name", "baz"}});
  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo"}},
      /*request_size=*/0, Http::TestResponseHeaderMapImpl{{":status", "200"}},
      /*response_size=*/0,
      /*backend_idx=*/0, Http::TestResponseHeaderMapImpl{{"route-name", "foo"}});
  ;
}

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, ScopedRdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Test that a SRDS DiscoveryResponse is successfully processed.

TEST_P(ScopedRdsIntegrationTest, BasicSuccess) {
  constexpr absl::string_view scope_tmpl = R"EOF(
name: {}
route_configuration_name: {}
key:
  fragments:
    - string_key: {}
)EOF";
  const std::string scope_route1 = fmt::format(scope_tmpl, "foo_scope1", "foo_route1", "foo-route");
  const std::string scope_route2 = fmt::format(scope_tmpl, "foo_scope2", "foo_route1", "bar-route");

  constexpr absl::string_view route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";

  on_server_init_function_ = [&]() {
    createScopedRdsStream();
    sendSrdsResponse({scope_route1, scope_route2}, {scope_route1, scope_route2}, {}, "1");
    createRdsStream("foo_route1");
    // CreateRdsStream waits for connection which is fired by RDS subscription.
    sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  };
  initialize();
  registerTestServerPorts({"http"});

  // No scope key matches "xyz-route".
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=xyz-route"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();

  // Test "foo-route" and 'bar-route' both gets routed to cluster_0.
  test_server_->waitForCounter("http.config_test.rds.foo_route1.update_success", Ge(1));
  for (const std::string& scope_key : std::vector<std::string>{"foo-route", "bar-route"}) {
    sendRequestAndVerifyResponse(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/meh"},
                                       {":authority", "host"},
                                       {":scheme", "http"},
                                       {"Addr", fmt::format("x-foo-key={}", scope_key)}},
        456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", scope_key}}, 123,
        /*cluster_0*/ 0);
  }
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_attempt",
                               Ge( // update_attempt only increase after a response
                                   isDelta() ? 1 : 2));
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                               Ge(1));
  // The version gauge should be set to xxHash64("1").
  test_server_->waitForGauge("http.config_test.scoped_rds.foo-scoped-routes.version",
                             Eq(13237225503670494420UL));

  // The connection manager prefix and the scoped route configuration name are both tags, so
  // neither appears in the tag-extracted name; this holds in both stats modes.
  const std::string& hcm_prefix = Config::TagNames::get().HTTP_CONN_MANAGER_PREFIX;
  const std::string& scoped_rds_config = Config::TagNames::get().SCOPED_RDS_CONFIG;
  const std::string& rds_route_config = Config::TagNames::get().RDS_ROUTE_CONFIG;
  expectStatTags("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                 "http.scoped_rds.update_success",
                 {{hcm_prefix, "config_test"}, {scoped_rds_config, "foo-scoped-routes"}});
  expectStatTags("http.config_test.rds.foo_route1.update_success", "http.rds.update_success",
                 {{hcm_prefix, "config_test"}, {rds_route_config, "foo_route1"}});

  // Add a new scope scope_route3 with a brand new RouteConfiguration foo_route2.
  const std::string scope_route3 = fmt::format(scope_tmpl, "foo_scope3", "foo_route2", "baz-route");

  sendSrdsResponse({scope_route1, scope_route2, scope_route3}, /*added*/ {scope_route3}, {}, "2");
  test_server_->waitForCounter("http.config_test.rds.foo_route1.update_attempt", Ge(2));
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_1"), "3");
  test_server_->waitForCounter("http.config_test.rds.foo_route1.update_success", Ge(2));
  createRdsStream("foo_route2");
  test_server_->waitForCounter("http.config_test.rds.foo_route2.update_attempt", Ge(1));
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route2", "cluster_0"), "1");
  test_server_->waitForCounter("http.config_test.rds.foo_route2.update_success", Ge(1));
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                               Ge(2));
  // The version gauge should be set to xxHash64("2").
  test_server_->waitForGauge("http.config_test.scoped_rds.foo-scoped-routes.version",
                             Eq(6927017134761466251UL));
  // After RDS update, requests within scope 'foo_scope1' or 'foo_scope2' get routed to
  // 'cluster_1'.
  for (const std::string& scope_key : std::vector<std::string>{"foo-route", "bar-route"}) {
    sendRequestAndVerifyResponse(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/meh"},
                                       {":authority", "host"},
                                       {":scheme", "http"},
                                       {"Addr", fmt::format("x-foo-key={}", scope_key)}},
        456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", scope_key}}, 123,
        /*cluster_1*/ 1);
  }
  // Now requests within scope 'foo_scope3' get routed to 'cluster_0'.
  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", fmt::format("x-foo-key={}", "baz-route")}},
      456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "bluh"}}, 123,
      /*cluster_0*/ 0);

  // Delete foo_scope1 and requests within the scope gets 400s.
  sendSrdsResponse({scope_route2, scope_route3}, {}, {"foo_scope1"}, "3");
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                               Ge(3));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo-route"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();
  // Add a new scope foo_scope4.
  const std::string& scope_route4 =
      fmt::format(scope_tmpl, "foo_scope4", "foo_route4", "xyz-route");
  sendSrdsResponse({scope_route3, scope_route2, scope_route4}, {scope_route4}, {}, "4");
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                               Ge(4));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=xyz-route"}});
  ASSERT_TRUE(response->waitForEndStream());
  // Get 404 because RDS hasn't pushed route configuration "foo_route4" yet.
  // But scope is found and the Router::NullConfigImpl is returned.
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();

  // RDS updated foo_route4, requests with scope key "xyz-route" now hit cluster_1.
  test_server_->waitForCounter("http.config_test.rds.foo_route4.update_attempt", Ge(1));
  createRdsStream("foo_route4");
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route4", "cluster_1"), "3");
  test_server_->waitForCounter("http.config_test.rds.foo_route4.update_success", Ge(1));
  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=xyz-route"}},
      456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "xyz-route"}}, 123,
      /*cluster_1 */ 1);
}

TEST_P(ScopedRdsIntegrationTest, SrdsWithInlineRouteConfiguration) {
  constexpr absl::string_view scoped_route_with_inline_route_config_tmpl = R"EOF(
name: {}
route_configuration:
  name: route_config
  virtual_hosts:
    - name: integration
      domains: ["*"]
      routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
key:
  fragments:
    - string_key: {}
)EOF";
  const std::string scoped_route1 = fmt::format(scoped_route_with_inline_route_config_tmpl,
                                                "scoped_route_0", "cluster_0", "foo-route");
  const std::string scoped_route2 = fmt::format(scoped_route_with_inline_route_config_tmpl,
                                                "scoped_route_0", "cluster_1", "bar-route");
  const std::string scoped_route3 = fmt::format(scoped_route_with_inline_route_config_tmpl,
                                                "scoped_route_1", "cluster_0", "foo-route");

  const std::string scoped_route_with_rds_subscription = R"EOF(
name: scoped_route_0
route_configuration_name: route_config
key:
  fragments:
    - string_key: foo-route
)EOF";
  const std::string route_config = R"EOF(
      name: route_config
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: { prefix: "/" }
          route: { cluster: cluster_1 }
)EOF";

  on_server_init_function_ = [&]() {
    createScopedRdsStream();
    sendSrdsResponse({scoped_route1}, {scoped_route1}, {}, "1");
  };
  initialize();
  registerTestServerPorts({"http"});

  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo-route"}},
      /*request_size=*/0, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "foo"}},
      /*response_size=*/0,
      /*backend_idx=*/0);

  // Replace foo-scope with a scoped route using an RDS subscription.
  sendSrdsResponse({scoped_route_with_rds_subscription}, {scoped_route_with_rds_subscription}, {},
                   "2");
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                               Ge(2));
  createRdsStream("route_config");
  test_server_->waitForCounter("http.config_test.rds.route_config.update_attempt", Ge(1));
  sendRdsResponse(route_config, "1");
  test_server_->waitForCounter("http.config_test.rds.route_config.update_success", Ge(1));

  // foo-route now goes to cluster_1.
  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo-route"}},
      /*request_size=*/0, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "foo"}},
      /*response_size=*/0,
      /*backend_idx=*/1);

  // Replace foo-scope with bar-scope, which uses an inlined RouteConfiguration.
  sendSrdsResponse({scoped_route2}, {scoped_route2}, {}, "3");
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                               Ge(3));

  // foo-route now returns 404.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo-route"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();

  // bar-route now returns 200 and is routed to cluster_1.
  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=bar-route"}},
      /*request_size=*/0, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "foo"}},
      /*response_size=*/0,
      /*backend_idx=*/1);
}

// Test that a bad config update updates the corresponding stats.
TEST_P(ScopedRdsIntegrationTest, ConfigUpdateFailure) {
  // 'name' will fail to validate due to empty string.
  const std::string scope_route1 = R"EOF(
name:
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: foo
)EOF";
  on_server_init_function_ = [this, &scope_route1]() {
    createScopedRdsStream();
    sendSrdsResponse({scope_route1}, {scope_route1}, {}, "1");
  };
  initialize();

  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_rejected",
                               Ge(1));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();

  // SRDS update fixed the problem.
  const std::string scope_route2 = R"EOF(
name: foo_scope1
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: foo
)EOF";
  sendSrdsResponse({scope_route2}, {scope_route2}, {}, "1");
  test_server_->waitForCounter("http.config_test.rds.foo_route1.update_attempt", Ge(1));
  createRdsStream("foo_route1");
  constexpr absl::string_view route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  test_server_->waitForCounter("http.config_test.rds.foo_route1.update_success", Ge(1));
  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo"}},
      456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "bluh"}}, 123,
      /*cluster_0*/ 0);
  cleanupUpstreamAndDownstream();
}

TEST_P(ScopedRdsIntegrationTest, RejectUnknownHttpFilterInPerFilterTypedConfig) {
  constexpr absl::string_view scope_tmpl = R"EOF(
name: {}
route_configuration_name: {}
key:
  fragments:
    - string_key: {}
)EOF";
  const std::string scope_route = fmt::format(scope_tmpl, "foo_scope1", "foo_route", "foo-route");

  constexpr absl::string_view route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
        typed_per_filter_config:
          filter.unknown:
            "@type": type.googleapis.com/google.protobuf.Struct
)EOF";

  on_server_init_function_ = [&]() {
    createScopedRdsStream();
    sendSrdsResponse({scope_route}, {scope_route}, {}, "1");
    createRdsStream("foo_route");
    // CreateRdsStream waits for connection which is fired by RDS subscription.
    sendRdsResponse(fmt::format(route_config_tmpl, "foo_route", "cluster_0"), "1");
  };
  initialize();
  registerTestServerPorts({"http"});

  test_server_->waitForCounter("http.config_test.rds.foo_route.update_rejected", Ge(1));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo-route"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();
}

TEST_P(ScopedRdsIntegrationTest, RejectKeyConflictInDeltaUpdate) {
  if (!isDelta()) {
    return;
  }
  const std::string scope_route1 = R"EOF(
name: foo_scope1
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: foo
)EOF";
  on_server_init_function_ = [this, &scope_route1]() {
    createScopedRdsStream();
    sendSrdsResponse({}, {scope_route1}, {}, "1");
  };
  initialize();
  // Delta SRDS update with key conflict, should be rejected.
  const std::string scope_route2 = R"EOF(
name: foo_scope2
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: foo
)EOF";
  sendSrdsResponse({}, {scope_route2}, {}, "2");
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_rejected",
                               Ge(1));
  sendSrdsResponse({}, {}, {"foo_scope1", "foo_scope2"}, "3");
}

// Verify SRDS works when reference via a xdstp:// collection locator.
TEST_P(ScopedRdsIntegrationTest, XdsTpCollection) {
  if (!isDelta()) {
    return;
  }
  const std::string scope_route1 = R"EOF(
name: xdstp://some/envoy.config.route.v3.ScopedRouteConfiguration/namespace/foo_scope1
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: foo
)EOF";
  constexpr absl::string_view route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  on_server_init_function_ = [this, &scope_route1, &route_config_tmpl]() {
    createScopedRdsStream();
    sendSrdsResponse({scope_route1}, {scope_route1}, {}, "1");
    createRdsStream("foo_route1");
    // CreateRdsStream waits for connection which is fired by RDS subscription.
    sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  };
  srds_resources_locator_ =
      "xdstp://some/envoy.config.route.v3.ScopedRouteConfiguration/namespace/*";
  initialize();
  registerTestServerPorts({"http"});

  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", fmt::format("x-foo-key={}", "foo")}},
      456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "cluster_0"}}, 123,
      /*cluster_0*/ 0);
}

// Two listeners with the same rds config but different scope_key_builder should use their own
// scope_key_builder to calculate the scope key and routes.
TEST_P(ScopedRdsIntegrationTest, ListenersWithDifferentScopeKeyBuilder) {
  constexpr absl::string_view scope_tmpl = R"EOF(
name: {}
route_configuration_name: {}
key:
  fragments:
    - string_key: {}
)EOF";
  const std::string scope_route1 = fmt::format(scope_tmpl, "foo_scope1", "foo_route1", "foo-route");
  const std::string scope_route2 = fmt::format(scope_tmpl, "foo_scope2", "foo_route1", "bar-route");

  constexpr absl::string_view route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";

  on_server_init_function_ = [&]() {
    createScopedRdsStream();
    sendSrdsResponse({scope_route1, scope_route2}, {scope_route1, scope_route2}, {}, "1");
    createRdsStream("foo_route1");
    // CreateRdsStream waits for connection which is fired by RDS subscription.
    sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  };
  // add listener_1 with different scope_key_builder
  add_listener_ = true;
  initialize();
  registerTestServerPorts({"http", "listener_1"});

  // listener_0 can't match using "," as separator
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key,foo-route"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();

  test_server_->waitForCounter("http.config_test.rds.foo_route1.update_success", Ge(1));

  // listener_0 can match using "=" as separator
  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo-route"}},
      456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "foo-route"}}, 123, 0);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200",
                 Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "foo-route"}},
                 std::string(123, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(456, upstream_request_->bodyLength());
  cleanupUpstreamAndDownstream();

  // listener_1 can't match using "=" as separator
  codec_client_ = makeHttpConnection(lookupPort("listener_1"));
  response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo-route"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();

  test_server_->waitForCounter("http.config_test.rds.foo_route1.update_success", Ge(1));

  // listener_1 can match using "," as separator
  codec_client_ = makeHttpConnection(lookupPort("listener_1"));
  response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key,foo-route"}},
      456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "foo-route"}}, 123, 0);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200",
                 Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "foo-route"}},
                 std::string(123, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(456, upstream_request_->bodyLength());
  cleanupUpstreamAndDownstream();

  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_attempt",
                               Ge( // update_attempt only increase after a response
                                   isDelta() ? 1 : 2));
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                               Ge(1));
  // The version gauge should be set to xxHash64("1").
  test_server_->waitForGauge("http.config_test.scoped_rds.foo-scoped-routes.version",
                             Eq(13237225503670494420UL));
}

#ifdef ENVOY_ADMIN_FUNCTIONALITY
// Scoped RDS end to end (real Envoy server, real SRDS gRPC stream, real HTTP traffic and admin
// config dump), verifying the fix behind envoy.reloadable_features.srds_partial_acceptance.
//
// A scope whose inline route points at a cluster unknown to the ClusterManager makes the SRDS push
// that carries it fail half-way through ScopedRdsConfigSubscription::addOrUpdateScopes():
//  * when srds_partial_acceptance is false:
//    - the scopes processed before it are written to the main-thread scoped_route_map_ (which backs
//      /config_dump), but the push is rejected before they are propagated to the workers;
//    - once the bad scope is gone, re-sending those scopes is acked, but they are skipped as
//      unchanged (their hash matches the stale map entry), so the workers keep returning 404
//      (NoRouteFound);
//    - the scopes after it in the same push are never processed, so they are unaffected and are
//      applied normally once re-sent.
//  * when srds_partial_acceptance is true:
//    - the bad scope is caught and rejected, while both valid_scope (before it) and other_scope
//      (after it) are written to scoped_route_map_ AND propagated to worker threads;
//    - once the bad scope is removed, the next push is acked and both valid scopes continue routing
//      traffic on the worker threads.
class ScopedRdsPartiallyRejectedPushIntegrationTest : public ScopedRdsIntegrationTest {
protected:
  // A scope with an inline route configuration that routes
  // all requests to `cluster`.
  static std::string scope(absl::string_view name, absl::string_view cluster,
                           absl::string_view key) {
    return absl::Substitute(R"EOF(
name: $0
route_configuration:
  name: $0_routes
  virtual_hosts:
    - name: integration
      domains: ["*"]
      routes:
        - match: { prefix: "/" }
          route: { cluster: $1 }
key:
  fragments:
    - string_key: $2
)EOF",
                            name, cluster, key);
  }

  static Http::TestRequestHeaderMapImpl requestInScope(absl::string_view scope_key) {
    return Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                          {":path", "/"},
                                          {":authority", "host"},
                                          {":scheme", "http"},
                                          {"Addr", absl::StrCat("x-foo-key=", scope_key)}};
  }

  // Expects a request in the given scope to be routed to fake upstream `backend_idx`.
  void expectRouted(absl::string_view scope_key, uint32_t backend_idx) {
    sendRequestAndVerifyResponse(requestInScope(scope_key), /*request_size=*/0,
                                 Http::TestResponseHeaderMapImpl{{":status", "200"}},
                                 /*response_size=*/0, backend_idx);
  }

  // Expects the workers to have no route for a request in the given scope (404, NoRouteFound).
  void expectNoRoute(absl::string_view scope_key) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(requestInScope(scope_key));
    ASSERT_TRUE(response->waitForEndStream());
    verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
    cleanupUpstreamAndDownstream();
  }

  // Reads the requests Envoy sent on the SRDS stream until a NACK, and returns its error message.
  std::string waitForSrdsNack() {
    FakeStream& stream = *scoped_rds_upstream_info_.stream_by_resource_name_[srds_config_name_];
    while (true) {
      if (isDelta()) {
        envoy::service::discovery::v3::DeltaDiscoveryRequest request;
        AssertionResult result = stream.waitForGrpcMessage(*dispatcher_, request);
        RELEASE_ASSERT(result, result.message());
        if (request.has_error_detail()) {
          return request.error_detail().message();
        }
      } else {
        envoy::service::discovery::v3::DiscoveryRequest request;
        AssertionResult result = stream.waitForGrpcMessage(*dispatcher_, request);
        RELEASE_ASSERT(result, result.message());
        if (request.has_error_detail()) {
          return request.error_detail().message();
        }
      }
    }
  }

  // Names of the scopes that the admin /config_dump lists for the SRDS subscription. The dump is
  // built from the main-thread scoped_route_map_, not from the config the workers route with.
  std::vector<std::string> scopeNamesInConfigDump() {
    BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", "/config_dump?resource=dynamic_scoped_route_configs", "",
        downstreamProtocol(), version_);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    envoy::admin::v3::ConfigDump config_dump;
    TestUtility::loadFromJson(response->body(), config_dump);
    std::vector<std::string> names;
    for (const auto& config : config_dump.configs()) {
      const auto dynamic_configs = MessageUtil::anyConvert<
          envoy::admin::v3::ScopedRoutesConfigDump::DynamicScopedRouteConfigs>(config);
      if (dynamic_configs.name() != srds_config_name_) {
        continue;
      }
      for (const auto& scoped_route_config : dynamic_configs.scoped_route_configs()) {
        names.push_back(MessageUtil::anyConvert<envoy::config::route::v3::ScopedRouteConfiguration>(
                            scoped_route_config)
                            .name());
      }
    }
    return names;
  }

  void testPartiallyRejectedSrdsPush(bool partial_acceptance);
};

void ScopedRdsPartiallyRejectedPushIntegrationTest::testPartiallyRejectedSrdsPush(
    bool partial_acceptance) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.srds_partial_acceptance",
                                    partial_acceptance ? "true" : "false");

  // cluster_0 is a static cluster of the fixture, backed by fake upstream 0.
  // Unrelated to the bug; only gives the server a working scope to start with.
  const std::string init_scope = scope("init_scope", "cluster_0", "init-key");
  const std::string valid_scope = scope("valid_scope", "cluster_0", "valid-key");
  // Routes to a cluster that does not exist.
  const std::string bad_scope = scope("bad_scope", "unknown_cluster", "bad-key");
  const std::string other_scope = scope("other_scope", "cluster_0", "other-key");

  // The server does not finish starting up until SRDS has answered once.
  on_server_init_function_ = [&]() {
    createScopedRdsStream();
    sendSrdsResponse({init_scope}, {init_scope}, {}, "1");
  };
  initialize();
  registerTestServerPorts({"http"});

  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                               Eq(1));
  EXPECT_THAT(scopeNamesInConfigDump(), ElementsAre("init_scope"));
  expectRouted("init-key", /*backend_idx=*/0);

  // Push 1: {valid_scope, bad_scope, other_scope} -> rejected.
  sendSrdsResponse({init_scope, valid_scope, bad_scope, other_scope},
                   {valid_scope, bad_scope, other_scope}, {}, "2");
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_rejected",
                               Eq(1));
  EXPECT_THAT(waitForSrdsNack(), HasSubstr("route: unknown cluster 'unknown_cluster'"));
  if (partial_acceptance) {
    // With srds_partial_acceptance enabled, both valid_scope and other_scope are added to
    // scoped_route_map_ AND propagated to worker threads.
    test_server_->waitForGauge("http.config_test.scoped_rds.foo-scoped-routes.all_scopes", Eq(3));
    EXPECT_THAT(scopeNamesInConfigDump(), ElementsAre("init_scope", "other_scope", "valid_scope"));
    expectRouted("valid-key", /*backend_idx=*/0);
    expectRouted("other-key", /*backend_idx=*/0);
    expectNoRoute("bad-key");
  } else {
    // BUG (when srds_partial_acceptance is false): valid_scope was recorded on the main thread
    // although the push was rejected, so the config dump lists it, while none of the push's scopes
    // reached the workers.
    EXPECT_THAT(scopeNamesInConfigDump(), ElementsAre("init_scope", "valid_scope"));
    expectNoRoute("valid-key");
    expectNoRoute("other-key");
  }
  expectRouted("init-key", /*backend_idx=*/0);

  // Push 2: bad_scope is removed and the other scopes are re-sent. acked.
  sendSrdsResponse({init_scope, valid_scope, other_scope}, {valid_scope, other_scope},
                   {"bad_scope"}, "3");
  test_server_->waitForCounter("http.config_test.scoped_rds.foo-scoped-routes.update_success",
                               Eq(2));
  test_server_->waitForGauge("http.config_test.scoped_rds.foo-scoped-routes.all_scopes", Eq(3));
  EXPECT_THAT(scopeNamesInConfigDump(), ElementsAre("init_scope", "other_scope", "valid_scope"));
  expectRouted("other-key", /*backend_idx=*/0);
  if (partial_acceptance) {
    expectRouted("valid-key", /*backend_idx=*/0);
  } else {
    // BUG (when srds_partial_acceptance is false): valid_scope was skipped as unchanged, so the
    // workers still have no route for it.
    expectNoRoute("valid-key");
  }
}

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, ScopedRdsPartiallyRejectedPushIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ScopedRdsPartiallyRejectedPushIntegrationTest,
       PartiallyRejectedSrdsPushLeavesScopesOffWorkers) {
  testPartiallyRejectedSrdsPush(/*partial_acceptance=*/false);
}

TEST_P(ScopedRdsPartiallyRejectedPushIntegrationTest,
       PartialAcceptanceAppliesValidScopesToWorkers) {
  testPartiallyRejectedSrdsPush(/*partial_acceptance=*/true);
}
#endif

} // namespace
} // namespace Envoy
