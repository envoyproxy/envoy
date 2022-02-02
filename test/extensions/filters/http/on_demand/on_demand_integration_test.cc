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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

using OnDemandScopedRdsIntegrationTest = ScopedRdsIntegrationTest;

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, OnDemandScopedRdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Test that a scoped route config update is performed on demand and http request will succeed.
TEST_P(OnDemandScopedRdsIntegrationTest, OnDemandUpdateSuccess) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  const std::string scope_route1 = R"EOF(
name: foo_scope1
route_configuration_name: foo_route1
on_demand: true
key:
  fragments:
    - string_key: foo
)EOF";
  on_server_init_function_ = [this, &scope_route1]() {
    createScopedRdsStream();
    sendSrdsResponse({scope_route1}, {scope_route1}, {}, "1");
  };
  initialize();
  registerTestServerPorts({"http"});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  // Request that match lazily loaded scope will trigger on demand loading.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo"}});
  createRdsStream("foo_route1");
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);

  waitForNextUpstreamRequest();
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  cleanupUpstreamAndDownstream();
}

// With on demand update filter configured, scope not match should still return 404
TEST_P(OnDemandScopedRdsIntegrationTest, OnDemandUpdateScopeNotMatch) {

  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");

  const std::string scope_tmpl = R"EOF(
name: {}
route_configuration_name: {}
key:
  fragments:
    - string_key: {}
)EOF";
  const std::string scope_route1 = fmt::format(scope_tmpl, "foo_scope1", "foo_route1", "foo-route");

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/meh" }}
          route: {{ cluster: {} }}
)EOF";

  on_server_init_function_ = [&]() {
    createScopedRdsStream();
    sendSrdsResponse({scope_route1}, {scope_route1}, {}, "1");
    createRdsStream("foo_route1");
    // CreateRdsStream waits for connection which is fired by RDS subscription.
    sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  };
  initialize();
  registerTestServerPorts({"http"});

  // No scope key matches "bar".
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=bar"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();
}

// With on demand update filter configured, scope match but virtual host don't match, should still
// return 404
TEST_P(OnDemandScopedRdsIntegrationTest, OnDemandUpdatePrimaryVirtualHostNotMatch) {

  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");

  const std::string scope_tmpl = R"EOF(
name: {}
route_configuration_name: {}
key:
  fragments:
    - string_key: {}
)EOF";
  const std::string scope_route1 = fmt::format(scope_tmpl, "foo_scope1", "foo_route1", "foo-route");

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/meh" }}
          route: {{ cluster: {} }}
)EOF";

  on_server_init_function_ = [&]() {
    createScopedRdsStream();
    sendSrdsResponse({scope_route1}, {scope_route1}, {}, "1");
    createRdsStream("foo_route1");
    // CreateRdsStream waits for connection which is fired by RDS subscription.
    sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  };
  initialize();
  registerTestServerPorts({"http"});

  // No virtual host matches "neh".
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/neh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();
}

// With on demand update filter configured, scope match but virtual host don't match, should still
// return 404
TEST_P(OnDemandScopedRdsIntegrationTest, OnDemandUpdateVirtualHostNotMatch) {

  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");

  const std::string scope_route1 = R"EOF(
name: foo_scope
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: foo
)EOF";
  const std::string scope_route2 = R"EOF(
name: bar_scope
route_configuration_name: foo_route1
on_demand: true
key:
  fragments:
    - string_key: bar
)EOF";
  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/meh" }}
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

  // No scope key matches "bar".
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/neh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=bar"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();
}

// Eager and lazy scopes share the same route configuration
TEST_P(OnDemandScopedRdsIntegrationTest, DifferentPriorityScopeShareRoute) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");

  const std::string scope_route1 = R"EOF(
name: foo_scope
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: foo
)EOF";
  const std::string scope_route2 = R"EOF(
name: bar_scope
route_configuration_name: foo_route1
on_demand: true
key:
  fragments:
    - string_key: bar
)EOF";

  const std::string route_config_tmpl = R"EOF(
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
  codec_client_ = makeHttpConnection(lookupPort("http"));
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);
  cleanupUpstreamAndDownstream();
  // "foo" request should succeed because the foo scope is loaded eagerly by default.
  // "bar" request will initialize rds provider on demand and also succeed.
  for (const std::string& scope_key : std::vector<std::string>{"foo", "bar"}) {
    sendRequestAndVerifyResponse(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/meh"},
                                       {":authority", "host"},
                                       {":scheme", "http"},
                                       {"Addr", fmt::format("x-foo-key={}", scope_key)}},
        456, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", scope_key}}, 123, 0);
  }
}

TEST_P(OnDemandScopedRdsIntegrationTest, OnDemandUpdateAfterActiveStreamDestroyed) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  const std::string scope_route1 = R"EOF(
name: foo_scope1
route_configuration_name: foo_route1
on_demand: true
key:
  fragments:
    - string_key: foo
)EOF";
  on_server_init_function_ = [this, &scope_route1]() {
    createScopedRdsStream();
    sendSrdsResponse({scope_route1}, {scope_route1}, {}, "1");
  };
  initialize();
  registerTestServerPorts({"http"});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  // A request that match lazily loaded scope will trigger on demand loading.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/meh"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo"}});
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_attempt", 1);
  // Close the connection and destroy the active stream.
  cleanupUpstreamAndDownstream();
  // Push rds update, on demand updated callback is post to worker thread.
  // There is no exception thrown even when active stream is dead because weak_ptr can't be
  // locked.
  createRdsStream("foo_route1");
  sendRdsResponse(fmt::format(route_config_tmpl, "foo_route1", "cluster_0"), "1");
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);
}

} // namespace
} // namespace Envoy
