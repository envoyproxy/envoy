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
      Config::TypeUrl::get().VirtualHost, buildVirtualHost1(), {}, "2", vhds_stream_.get());
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_.get()));

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/one", "vhost.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/two", "vhost.second");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, {}, {"my_route/vhost_1", "my_route/vhost_2"}, "3",
      vhds_stream_.get());
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_.get()));

  // an upstream request to an (now) unknown domain
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.first")}, {},
                                           vhds_stream_.get()));
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "4", vhds_stream_.get(),
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
      Config::TypeUrl::get().VirtualHost, buildVirtualHost1(), {}, "2", vhds_stream_.get());
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_.get()));

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
      Config::TypeUrl::get().VirtualHost, {}, {"my_route/vhost_1", "my_route/vhost_2"}, "3",
      vhds_stream_.get());
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_.get()));

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
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.first")}, {},
                                           vhds_stream_.get()));
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "4", vhds_stream_.get(),
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
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
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
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
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
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
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
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
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
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                             {vhdsRequestResourceName("vhost.first")}, {},
                                             vhds_stream_.get()));
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "4", vhds_stream_.get(),
        {"my_route/vhost.first"});
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {},
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
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                             {vhdsRequestResourceName("vhost.second")}, {},
                                             vhds_stream_.get()));
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TypeUrl::get().VirtualHost,
        {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
            virtualHostYaml("my_route/vhost_2", "vhost.second"))},
        {}, "4", vhds_stream_.get(), {"my_route/vhost.second"});
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {},
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
        Config::TypeUrl::get().VirtualHost,
        {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
             fmt::format(VhostTemplateAfterUpdate, "my_route/vhost_1", "vhost.first")),
         TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
             fmt::format(VhostTemplateAfterUpdate, "my_route/vhost_2", "vhost.second"))},
        {}, "5", vhds_stream_.get());
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {},
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
      Config::TypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
           virtualHostYaml("my_route/vhost_1", "vhost.duplicate")),
       TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
           virtualHostYaml("my_route/vhost_2", "vhost.duplicate"))},
      {}, "2", vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get(), 13,
                                           "Only unique values for domains are permitted"));

  // Another update, this time valid, should result in no errors
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
          virtualHostYaml("my_route/vhost_3", "vhost.third"))},
      {}, "2", vhds_stream_.get());
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_.get()));

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
