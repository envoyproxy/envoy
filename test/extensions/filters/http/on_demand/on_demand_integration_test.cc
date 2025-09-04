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
#include "test/integration/vhds.h"
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

  constexpr absl::string_view route_config_tmpl = R"EOF(
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
                                     {":authority", "sni.lyft.com"},
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

  constexpr absl::string_view scope_tmpl = R"EOF(
name: {}
route_configuration_name: {}
key:
  fragments:
    - string_key: {}
)EOF";
  const std::string scope_route1 = fmt::format(scope_tmpl, "foo_scope1", "foo_route1", "foo-route");

  constexpr absl::string_view route_config_tmpl = R"EOF(
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
                                     {":authority", "sni.lyft.com"},
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

  constexpr absl::string_view scope_tmpl = R"EOF(
name: {}
route_configuration_name: {}
key:
  fragments:
    - string_key: {}
)EOF";
  const std::string scope_route1 = fmt::format(scope_tmpl, "foo_scope1", "foo_route1", "foo-route");

  constexpr absl::string_view route_config_tmpl = R"EOF(
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
                                     {":authority", "sni.lyft.com"},
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
  constexpr absl::string_view route_config_tmpl = R"EOF(
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
                                     {":authority", "sni.lyft.com"},
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
  codec_client_ = makeHttpConnection(lookupPort("http"));
  test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);
  cleanupUpstreamAndDownstream();
  // "foo" request should succeed because the foo scope is loaded eagerly by default.
  // "bar" request will initialize rds provider on demand and also succeed.
  for (const std::string& scope_key : std::vector<std::string>{"foo", "bar"}) {
    sendRequestAndVerifyResponse(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/meh"},
                                       {":authority", "sni.lyft.com"},
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

  constexpr absl::string_view route_config_tmpl = R"EOF(
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
                                     {":authority", "sni.lyft.com"},
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

class OnDemandVhdsIntegrationTest : public VhdsIntegrationTest {
  void initialize() override {
    config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
    VhdsIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, OnDemandVhdsIntegrationTest,
                         UNIFIED_LEGACY_GRPC_CLIENT_INTEGRATION_PARAMS);
// tests a scenario when:
//  - a spontaneous VHDS DiscoveryResponse adds two virtual hosts
//  - the next spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
//  - Upstream makes a request to an (now) unknown domain
//  - A VHDS DiscoveryResponse received containing update for the domain
//  - Upstream receives a 200 response
TEST_P(OnDemandVhdsIntegrationTest, VhdsVirtualHostAddUpdateRemove) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse adds two virtual hosts
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, buildVirtualHost1(), {}, "2", vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/one", "vhost.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/two", "vhost.second");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, {}, {"my_route/vhost_1", "my_route/vhost_2"}, "3",
      vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  // an upstream request to an (now) unknown domain
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.first")}, {},
                                           vhds_stream_.get()));
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "4", vhds_stream_.get(),
      {"my_route/vhost.first"});

  waitForNextUpstreamRequest(1);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - an RDS exchange contains a non-empty virtual_hosts array
//  - a spontaneous VHDS DiscoveryResponse adds two virtual hosts
//  - the next spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
//  - Upstream makes a request to an (now) unknown domain
//  - A VHDS DiscoveryResponse received containing update for the domain
//  - Upstream receives a 200 response
TEST_P(OnDemandVhdsIntegrationTest, RdsWithVirtualHostsVhdsVirtualHostAddUpdateRemove) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse adds two virtual hosts
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, buildVirtualHost1(), {}, "2", vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  // verify that rds-based virtual host can be resolved
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/one", "vhost.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/two", "vhost.second");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse removes virtual hosts added via vhds
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, {}, {"my_route/vhost_1", "my_route/vhost_2"}, "3",
      vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  // verify rds-based virtual host is still present
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.first")}, {},
                                           vhds_stream_.get()));
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "4", vhds_stream_.get(),
      {"my_route/vhost.first"});

  waitForNextUpstreamRequest(1);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// tests a scenario where:
//  a Resource received in a DeltaDiscoveryResponse has name that matches the value used in the
//  on-demand request
TEST_P(OnDemandVhdsIntegrationTest, VhdsOnDemandUpdateWithResourceNameAsAlias) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // verify that rds-based virtual host can be resolved
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Attempt to make a request to an unknown host
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost_1"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost_1")}, {},
                                           vhds_stream_.get()));

  envoy::service::discovery::v3::DeltaDiscoveryResponse vhds_update =
      createDeltaDiscoveryResponseWithResourceNameUsedAsAlias();
  vhds_stream_->sendGrpcMessage(vhds_update);

  waitForNextUpstreamRequest(1);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - an RDS exchange contains a non-empty virtual_hosts array
//  - a spontaneous VHDS DiscoveryResponse adds two virtual hosts
//  - Upstream makes a request to an unknown domain
//  - A VHDS DiscoveryResponse received but contains no update for the domain (the management server
//  couldn't resolve it)
//  - Upstream receives a 404 response
TEST_P(OnDemandVhdsIntegrationTest, VhdsOnDemandUpdateFailToResolveTheAlias) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // verify that rds-based virtual host can be resolved
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Attempt to make a request to an unknown host
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.third"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.third")}, {},
                                           vhds_stream_.get()));
  // Send an empty response back (the management server isn't aware of vhost.third)
  notifyAboutAliasResolutionFailure("4", vhds_stream_, {"my_route/vhost.third"});

  response->waitForHeaders();
  EXPECT_EQ("404", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - an RDS exchange contains a non-empty virtual_hosts array
//  - Upstream makes a request to vhost.third that cannot be resolved by the management server
//  - Management server sends a spontaneous update for vhost.first and an empty response for
//  vhost.third
//  - Upstream receives a 404 response
TEST_P(OnDemandVhdsIntegrationTest, VhdsOnDemandUpdateFailToResolveOneAliasOutOfSeveral) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // verify that rds-based virtual host can be resolved
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Attempt to make a request to an unknown host
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.third"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.third")}, {},
                                           vhds_stream_.get()));
  // Send an empty response back (the management server isn't aware of vhost.third)
  sendDeltaDiscoveryResponseWithUnresolvedAliases({buildVirtualHost2()}, {}, "4", vhds_stream_,
                                                  {"my_route/vhost.first"},
                                                  {"my_route/vhost.third"});

  response->waitForHeaders();
  EXPECT_EQ("404", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Verify that an vhds update succeeds even when the client closes its connection
TEST_P(OnDemandVhdsIntegrationTest, VhdsOnDemandUpdateHttpConnectionCloses) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  EXPECT_TRUE(codec_client_->waitForDisconnect());

  // Attempt to make a request to an unknown host
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost_1"},
                                                 {"x-lyft-user-id", "123"}};
  auto encoder_decoder = codec_client_->startRequest(request_headers);
  Http::RequestEncoder& encoder = encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost_1")}, {},
                                           vhds_stream_.get()));

  envoy::service::discovery::v3::DeltaDiscoveryResponse vhds_update =
      createDeltaDiscoveryResponseWithResourceNameUsedAsAlias();
  vhds_stream_->sendGrpcMessage(vhds_update);

  codec_client_->sendReset(encoder);
  ASSERT_TRUE(response->waitForReset());
  EXPECT_TRUE(codec_client_->connected());

  cleanupUpstreamAndDownstream();
}

constexpr absl::string_view VhostTemplateAfterUpdate = R"EOF(
name: {}
domains: [{}]
routes:
- match: {{ prefix: "/after_update" }}
  route: {{ cluster: "my_service" }}
)EOF";

// Verifies that after multiple vhds updates, virtual hosts from earlier updates still can receive
// updates See https://github.com/envoyproxy/envoy/issues/12158 for more details
TEST_P(OnDemandVhdsIntegrationTest, MultipleUpdates) {
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  EXPECT_TRUE(codec_client_->waitForDisconnect());

  {
    // make first vhds request (for vhost.first)
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/"},
                                                   {":scheme", "http"},
                                                   {":authority", "vhost.first"},
                                                   {"x-lyft-user-id", "123"}};
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                             {vhdsRequestResourceName("vhost.first")}, {},
                                             vhds_stream_.get()));
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TestTypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "4", vhds_stream_.get(),
        {"my_route/vhost.first"});
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));

    waitForNextUpstreamRequest(1);
    // Send response headers, and end_stream if there is no response body.
    upstream_request_->encodeHeaders(default_response_headers_, true);

    response->waitForHeaders();
    EXPECT_EQ("200", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
    EXPECT_TRUE(codec_client_->waitForDisconnect());
  }
  {
    // make second vhds request (for vhost.second)
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/"},
                                                   {":scheme", "http"},
                                                   {":authority", "vhost.second"},
                                                   {"x-lyft-user-id", "123"}};
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                             {vhdsRequestResourceName("vhost.second")}, {},
                                             vhds_stream_.get()));
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TestTypeUrl::get().VirtualHost,
        {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
            virtualHostYaml("my_route/vhost_2", "vhost.second"))},
        {}, "4", vhds_stream_.get(), {"my_route/vhost.second"});
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));

    waitForNextUpstreamRequest(1);
    // Send response headers, and end_stream if there is no response body.
    upstream_request_->encodeHeaders(default_response_headers_, true);

    response->waitForHeaders();
    EXPECT_EQ("200", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
    EXPECT_TRUE(codec_client_->waitForDisconnect());
  }
  {
    // Attempt to push updates for both vhost.first and vhost.second
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TestTypeUrl::get().VirtualHost,
        {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
             fmt::format(VhostTemplateAfterUpdate, "my_route/vhost_1", "vhost.first")),
         TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
             fmt::format(VhostTemplateAfterUpdate, "my_route/vhost_2", "vhost.second"))},
        {}, "5", vhds_stream_.get());
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));

    // verify that both virtual hosts have been updated
    testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/after_update", "vhost.first");
    cleanupUpstreamAndDownstream();
    ASSERT_TRUE(codec_client_->waitForDisconnect());

    testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/after_update", "vhost.second");
    cleanupUpstreamAndDownstream();
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  }
}

// Verifies that invalid VHDS updates get rejected and do not affect subsequent updates
// see https://github.com/envoyproxy/envoy/issues/14918
TEST_P(OnDemandVhdsIntegrationTest, AttemptAddingDuplicateDomainNames) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse with duplicate domains that results in an error in the
  // ack.
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
           virtualHostYaml("my_route/vhost_1", "vhost.duplicate")),
       TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
           virtualHostYaml("my_route/vhost_2", "vhost.duplicate"))},
      {}, "2", vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get(), 13,
                                           "Only unique values for domains are permitted"));

  // Another update, this time valid, should result in no errors
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
          virtualHostYaml("my_route/vhost_3", "vhost.third"))},
      {}, "2", vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  cleanupUpstreamAndDownstream();
}

// Test that on-demand VHDS works correctly with internal redirects for requests with body
TEST_P(OnDemandVhdsIntegrationTest, OnDemandVhdsWithInternalRedirectAndRequestBody) {
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Create a virtual host configuration that supports internal redirects
  const std::string vhost_with_redirect = R"EOF(
name: my_route/vhost_redirect
domains:
- vhost.redirect
routes:
- match:
    prefix: "/"
  name: redirect_route
  route:
    cluster: my_service
    internal_redirect_policy: {}
)EOF";

  // Make a POST request with body to an unknown domain that will trigger VHDS
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},      {":path", "/"},
      {":scheme", "http"},      {":authority", "vhost.redirect"},
      {"content-length", "12"}, {"x-lyft-user-id", "123"}};
  const std::string request_body = "test_payload";

  IntegrationStreamDecoderPtr response =
      codec_client_->makeRequestWithBody(request_headers, request_body);

  // Expect VHDS request for the unknown domain
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.redirect")}, {},
                                           vhds_stream_.get()));

  // Send VHDS response with the virtual host that supports redirects
  auto vhost_config =
      TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(vhost_with_redirect);
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, {vhost_config}, {}, "2", vhds_stream_.get(),
      {"my_route/vhost.redirect"});

  // Wait for the first upstream request (original request)
  waitForNextUpstreamRequest(1);
  EXPECT_EQ(request_body, upstream_request_->body().toString());
  EXPECT_EQ("vhost.redirect", upstream_request_->headers().getHostValue());

  // Respond with a redirect to the same host but different path
  Http::TestResponseHeaderMapImpl redirect_response{
      {":status", "302"},
      {"content-length", "0"},
      {"location", "http://vhost.redirect/redirected/path"},
      {"test-header", "redirect-value"}};
  upstream_request_->encodeHeaders(redirect_response, true);

  // Wait for the second upstream request (after redirect)
  waitForNextUpstreamRequest(1);
  EXPECT_EQ(request_body, upstream_request_->body().toString());
  EXPECT_EQ("vhost.redirect", upstream_request_->headers().getHostValue());
  EXPECT_EQ("/redirected/path", upstream_request_->headers().getPathValue());
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_EQ("http://vhost.redirect/", upstream_request_->headers().getEnvoyOriginalUrlValue());

  // Send final response
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Verify the response
  response->waitForHeaders();
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify internal redirect succeeded
  EXPECT_EQ(1,
            test_server_->counter("cluster.my_service.upstream_internal_redirect_succeeded_total")
                ->value());
  // 302 was never returned downstream
  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  // We expect 2 total 2xx responses: one from initial test + one from our VHDS redirect test
  EXPECT_EQ(2, test_server_->counter("http.config_test.downstream_rq_2xx")->value());

  cleanupUpstreamAndDownstream();
}

} // namespace

namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

using OnDemandIntegrationTest = VhdsIntegrationTest;

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, OnDemandIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Integration test specifically for the GitHub issue #17891 fix:
// Verify that VHDS requests with body don't result in 404 responses
TEST_P(OnDemandIntegrationTest, VhdsWithRequestBodyShouldNotReturn404) {
  autonomous_upstream_ = true;
  initialize();

  // Send a POST request with body to trigger VHDS discovery
  const std::string request_body = "test request body data";
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", fmt::format("vhds.example.com:{}", lookupPort("http"))},
      }, 
      request_body);

  // Wait for the response - this should NOT be a 404
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  
  // Before the fix, this would be "404" due to route being nullptr
  // After the fix, this should be "200" from the upstream
  EXPECT_EQ("200", response->headers().getStatusValue());
  
  // Verify the upstream received the full request body
  EXPECT_EQ(request_body, response->body());
}

// Integration test for requests without body (should still work)
TEST_P(OnDemandIntegrationTest, VhdsWithoutBodyStillWorksCorrectly) {
  autonomous_upstream_ = true;
  initialize();

  // Send a GET request without body
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", fmt::format("vhds.example.com:{}", lookupPort("http"))},
      });

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Integration test for large request bodies to ensure proper buffering
TEST_P(OnDemandIntegrationTest, VhdsWithLargeRequestBodyBuffersCorrectly) {
  autonomous_upstream_ = true;
  initialize();

  // Create a large request body to test buffering behavior
  std::string large_body(10000, 'x'); // 10KB of 'x' characters
  
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/test"},  
          {":scheme", "http"},
          {":authority", fmt::format("vhds.example.com:{}", lookupPort("http"))},
      },
      large_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  
  // Verify the entire large body was properly buffered and forwarded
  EXPECT_EQ(large_body, response->body());
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
