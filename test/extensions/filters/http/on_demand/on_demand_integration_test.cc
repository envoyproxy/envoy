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
#include "test/integration/http_protocol_integration.h"
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

// Test class for VHDS on-demand updates with request bodies
class OnDemandVhdsWithBodyIntegrationTest
    : public testing::TestWithParam<
          std::tuple<HttpProtocolTestParams, std::tuple<Network::Address::IpVersion,
                                                        Grpc::ClientType, Grpc::LegacyOrUnified>>>,
      public HttpIntegrationTest {
public:
  using ParamType =
      std::tuple<HttpProtocolTestParams,
                 std::tuple<Network::Address::IpVersion, Grpc::ClientType, Grpc::LegacyOrUnified>>;

  const HttpProtocolTestParams& httpProtocolParams() const { return std::get<0>(GetParam()); }

  OnDemandVhdsWithBodyIntegrationTest()
      : HttpIntegrationTest(httpProtocolParams().downstream_protocol, httpProtocolParams().version,
                            ConfigHelper::httpProxyConfig(
                                /*downstream_is_quic=*/httpProtocolParams().downstream_protocol ==
                                Http::CodecType::HTTP3)),
        use_universal_header_validator_(httpProtocolParams().use_universal_header_validator) {
    setupHttp2ImplOverrides(httpProtocolParams().http2_implementation);
    config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                      use_universal_header_validator_ ? "true" : "false");
    use_lds_ = false;
    config_helper_.addRuntimeOverride(
        "envoy.reloadable_features.unified_mux",
        std::get<2>(std::get<1>(GetParam())) == Grpc::LegacyOrUnified::Unified ? "true" : "false");
    config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  }

  void SetUp() override {
    setDownstreamProtocol(httpProtocolParams().downstream_protocol);
    setUpstreamProtocol(httpProtocolParams().upstream_protocol);
  }

  void TearDown() override { cleanUpXdsConnection(); }

  std::string virtualHostYaml(const std::string& name, const std::string& domain) {
    return fmt::format(R"EOF(
name: {}
domains: [{}]
routes:
- match: {{ prefix: "/" }}
  route: {{ cluster: "cluster_0" }}
)EOF",
                       name, domain);
  }

  std::string vhdsRequestResourceName(const std::string& host_header) {
    return "my_route/" + host_header;
  }

  envoy::config::route::v3::VirtualHost buildVirtualHost(const std::string& name,
                                                         const std::string& domain) {
    return TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
        virtualHostYaml(name, domain));
  }

  void initialize() override {
    setUpstreamCount(2);                         // xds_cluster and cluster_0
    setUpstreamProtocol(Http::CodecType::HTTP2); // xDS uses gRPC uses HTTP2

    const auto ip_version = httpProtocolParams().version;
    config_helper_.addConfigModifier([ip_version](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add xds_cluster for VHDS
      auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      xds_cluster->MergeFrom(ConfigHelper::buildStaticCluster(
          "xds_cluster", /*port=*/0, Network::Test::getLoopbackAddressString(ip_version)));
      ConfigHelper::setHttp2(*xds_cluster);

      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

      ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::http_connection_manager::v3::
                                      HttpConnectionManager>());
      auto hcm_config = MessageUtil::anyConvert<
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
          *config_blob);

      // Use RDS instead of static route_config so Envoy connects to xDS server
      hcm_config.clear_route_config();
      auto* rds_config = hcm_config.mutable_rds();
      rds_config->set_route_config_name("my_route");
      auto* rds_config_source = rds_config->mutable_config_source();
      rds_config_source->mutable_api_config_source()->set_api_type(
          envoy::config::core::v3::ApiConfigSource::GRPC);
      rds_config_source->mutable_api_config_source()
          ->add_grpc_services()
          ->mutable_envoy_grpc()
          ->set_cluster_name("xds_cluster");

      config_blob->PackFrom(hcm_config);
    });

    HttpIntegrationTest::initialize();

    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});

    // Set up xDS connection (xds_cluster is the second cluster, so it maps to fake_upstreams_[1])
    auto result = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();

    EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "",
                                            {"my_route"}, true));
    envoy::config::route::v3::RouteConfiguration route_config;
    route_config.set_name("my_route");
    auto* vhds_config_source = route_config.mutable_vhds()->mutable_config_source();
    vhds_config_source->mutable_api_config_source()->set_api_type(
        envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
    vhds_config_source->mutable_api_config_source()
        ->add_grpc_services()
        ->mutable_envoy_grpc()
        ->set_cluster_name("xds_cluster");
    sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
        Config::TestTypeUrl::get().RouteConfiguration, {route_config}, "1");

    result = xds_connection_->waitForNewStream(*dispatcher_, vhds_stream_);
    RELEASE_ASSERT(result, result.message());
    vhds_stream_->startGrpcStream();

    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));
  }

  FakeStreamPtr vhds_stream_;

protected:
  const bool use_universal_header_validator_;
};

INSTANTIATE_TEST_SUITE_P(
    ProtocolsAndGrpcTypes, OnDemandVhdsWithBodyIntegrationTest,
    testing::Combine(
        testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
        UNIFIED_LEGACY_GRPC_CLIENT_INTEGRATION_PARAMS),
    [](const testing::TestParamInfo<
        std::tuple<HttpProtocolTestParams, std::tuple<Network::Address::IpVersion, Grpc::ClientType,
                                                      Grpc::LegacyOrUnified>>>& info) {
      return absl::StrCat(
          HttpProtocolIntegrationTest::protocolTestParamsToString(
              testing::TestParamInfo<HttpProtocolTestParams>(std::get<0>(info.param), 0)),
          "_",
          Grpc::UnifiedOrLegacyMuxIntegrationParamTest::protocolTestParamsToString(
              testing::TestParamInfo<
                  std::tuple<Network::Address::IpVersion, Grpc::ClientType, Grpc::LegacyOrUnified>>(
                  std::get<1>(info.param), 0)));
    });

// Test VHDS on-demand update with a request body
TEST_P(OnDemandVhdsWithBodyIntegrationTest, VhdsOnDemandUpdateWithBody) {
  // TODO(wdauchy): Fix Unified mux to properly handle on-demand VHDS updates.
  const bool is_unified_mux =
      std::get<2>(std::get<1>(GetParam())) == Grpc::LegacyOrUnified::Unified;
  if (is_unified_mux) {
    GTEST_SKIP() << "Unified mux times out when processing on-demand VHDS updates";
  }
  initialize();
  // Make a request with body to an unknown virtual host
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.with.body"},
                                                 {"x-lyft-user-id", "123"}};
  const std::string request_body = "test request body";
  IntegrationStreamDecoderPtr response =
      codec_client_->makeRequestWithBody(request_headers, request_body, true);

  // Verify VHDS on-demand request is sent
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.with.body")}, {},
                                           vhds_stream_.get()));

  // Send VHDS response with the virtual host
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost,
      {buildVirtualHost("my_route/vhost_with_body", "vhost.with.body")}, {}, "2",
      vhds_stream_.get(), {"my_route/vhost.with.body"});

  // Wait for VHDS ACK to ensure the response is processed
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  // Wait for upstream request (cluster_0 is the first cluster, so it maps to fake_upstreams_[0])
  waitForNextUpstreamRequest(0);

  // Verify the request body was received correctly
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_body, upstream_request_->body().toString());

  // Send response
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
