#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/api_version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/scoped_rds.h"
#include "test/test_common/printers.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class InlineScopedRoutesIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  InlineScopedRoutesIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

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
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);
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
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_attempt",
                                 // update_attempt only increase after a response
                                 isDelta() ? 1 : 2);
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 1);
  // The version gauge should be set to xxHash64("1").
  test_server_->waitForGaugeEq("http.config_test.scoped_rds.foo-scoped-routes.version",
                               13237225503670494420UL);

  // Add a new scope scope_route3 with a brand new RouteConfiguration foo_route2.
  const std::string scope_route3 = fmt::format(scope_tmpl, "foo_scope3", "foo_route2", "baz-route");

  sendSrdsResponse({scope_route1, scope_route2, scope_route3}, /*added*/ {scope_route3}, {}, "2");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_attempt", 2);
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_1"), "3");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 2);
  createRdsStream("foo_route2");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route2.update_attempt", 1);
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route2", "cluster_0"), "1");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route2.update_success", 1);
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 2);
  // The version gauge should be set to xxHash64("2").
  test_server_->waitForGaugeEq("http.config_test.scoped_rds.foo-scoped-routes.version",
                               6927017134761466251UL);
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
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 3);
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
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 4);
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
  test_server_->waitForCounterGe("http.config_test.rds.foo_route4.update_attempt", 1);
  createRdsStream("foo_route4");
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route4", "cluster_1"), "3");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route4.update_success", 1);
  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=xyz-route"}},
      456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "xyz-route"}}, 123,
      /*cluster_1 */ 1);
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

  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_rejected",
                                 1);
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
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_attempt", 1);
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
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);
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

  test_server_->waitForCounterGe("http.config_test.rds.foo_route.update_rejected", 1);
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
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_rejected",
                                 1);
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

  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);

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

  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);

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

  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_attempt",
                                 // update_attempt only increase after a response
                                 isDelta() ? 1 : 2);
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 1);
  // The version gauge should be set to xxHash64("1").
  test_server_->waitForGaugeEq("http.config_test.scoped_rds.foo-scoped-routes.version",
                               13237225503670494420UL);
}

} // namespace
} // namespace Envoy
