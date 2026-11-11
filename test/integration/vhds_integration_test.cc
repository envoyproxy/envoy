#include <functional>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/scope.h"

#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/integration/vhds.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

// TODO (dmitri-d) move config yaml into ConfigHelper
const char RdsWithoutVhdsConfig[] = R"EOF(
name: my_route
virtual_hosts:
- name: vhost_rds1
  domains: ["vhost.rds.first"]
  routes:
  - match: { prefix: "/rdsone" }
    route: { cluster: my_service }
)EOF";

class VhdsInitializationTest : public HttpIntegrationTest,
                               public Grpc::UnifiedOrLegacyMuxIntegrationParamTest {
public:
  VhdsInitializationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(), config()) {
    use_lds_ = false;
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      isUnified() ? "true" : "false");
  }

  void TearDown() override { cleanUpXdsConnection(); }

  // Overridden to insert this stuff into the initialize() at the very beginning of
  // HttpIntegrationTest::testRouterRequestAndResponseWithBody().
  void initialize() override {
    // Controls how many addFakeUpstream() will happen in
    // BaseIntegrationTest::createUpstreams() (which is part of initialize()).
    // Make sure this number matches the size of the 'clusters' repeated field in the bootstrap
    // config that you use!
    setUpstreamCount(2);                         // the CDS cluster
    setUpstreamProtocol(Http::CodecType::HTTP2); // CDS uses gRPC uses HTTP2.

    // BaseIntegrationTest::initialize() does many things:
    // 1) It appends to fake_upstreams_ as many as you asked for via setUpstreamCount().
    // 2) It updates your bootstrap config with the ports your fake upstreams are actually listening
    //    on (since you're supposed to leave them as 0).
    // 3) It creates and starts an IntegrationTestServer - the thing that wraps the almost-actual
    //    Envoy used in the tests.
    // 4) Bringing up the server usually entails waiting to ensure that any listeners specified in
    //    the bootstrap config have come up, and registering them in a port map (see lookupPort()).
    //    However, this test needs to defer all of that to later.
    defer_listener_finalization_ = true;
    HttpIntegrationTest::initialize();

    // Now that the upstream has been created, process Envoy's request to discover it.
    // (First, we have to let Envoy establish its connection to the RDS server.)
    AssertionResult result = // xds_connection_ is filled with the new FakeHttpConnection.
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();

    EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "",
                                            {"my_route"}, true));
    sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
        Config::TestTypeUrl::get().RouteConfiguration,
        {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(
            RdsWithoutVhdsConfig)},
        "1");

    // Wait for our statically specified listener to become ready, and register its port in the
    // test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  FakeStreamPtr vhds_stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, VhdsInitializationTest,
                         UNIFIED_LEGACY_GRPC_CLIENT_INTEGRATION_PARAMS);

// tests a scenario when:
//  - RouteConfiguration without VHDS is received
//  - RouteConfiguration update with VHDS configuration in it is received
//  - Upstream makes a request to a VirtualHost in the VHDS update
TEST_P(VhdsInitializationTest, InitializeVhdsAfterRdsHasBeenInitialized) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Update RouteConfig, this time include VHDS config
  sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TestTypeUrl::get().RouteConfiguration,
      {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(RdsConfigWithVhosts)},
      "2");

  auto result = xds_connection_->waitForNewStream(*dispatcher_, vhds_stream_);
  RELEASE_ASSERT(result, result.message());
  vhds_stream_->startGrpcStream();

  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
          fmt::format(VhostTemplate, "my_route/vhost_0", "vhost.first"))},
      {}, "1", vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  // Confirm vhost.first that was configured via VHDS is reachable
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "vhost.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, VhdsIntegrationTest, VHDS_INTEGRATION_PARAMS,
                         vhdsTestParamsToString);

TEST_P(VhdsIntegrationTest, RdsUpdateWithoutVHDSChangesDoesNotRestartVHDS) {
  if (routeConfigType() != RouteConfigType::Rds) {
    GTEST_SKIP() << "This test requires RDS update";
  }
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "sni.lyft.com");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Update RouteConfig, but don't change VHDS config
  sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TestTypeUrl::get().RouteConfiguration,
      {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(RdsConfigWithVhosts)},
      "2");

  // Confirm vhost_0 that was originally configured via VHDS is reachable
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "sni.lyft.com");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// An inline ("static") route configuration that uses VHDS warms up with the listener it is
// configured in: the listener stays warming, and Envoy doesn't start its workers, until the
// initial VHDS response has delivered the virtual hosts the route configuration serves.
TEST_P(VhdsIntegrationTest, InitialVhdsBlocksListenerWarmingWithInlineRouteConfig) {
  if (routeConfigType() != RouteConfigType::Static) {
    GTEST_SKIP() << "This test covers the inline route configuration";
  }

  // Returns once Envoy has asked for the virtual hosts, i.e. once everything the inline route
  // configuration needs in order to go live has happened except the response itself.
  initializeUpToInitialVhdsRequest();

  // The listener is still warming: the inline route configuration hasn't gone live, so Envoy
  // hasn't started its workers.
  EXPECT_FALSE(workersStarted());

  // The virtual hosts arrive, so the route configuration goes live and the listener finishes
  // warming.
  sendInitialVhdsResponse();
  waitForListenerToServe();

  // The virtual host that only VHDS knows about is reachable, i.e. the listener didn't come up
  // serving a route configuration that was missing it.
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "sni.lyft.com");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// A dynamic (RDS) route configuration that uses VHDS warms up the same way: receiving the
// RouteConfiguration over RDS is not by itself enough to finish warming the listener, the initial
// VHDS response has to arrive first.
TEST_P(VhdsIntegrationTest, InitialVhdsBlocksListenerWarmingWithRdsRouteConfig) {
  if (routeConfigType() != RouteConfigType::Rds) {
    GTEST_SKIP() << "This test covers the dynamic route configuration";
  }

  // Returns once the RDS response has been delivered and Envoy has asked for the virtual hosts.
  initializeUpToInitialVhdsRequest();

  // The RouteConfiguration has already arrived over RDS and Envoy has started the VHDS
  // subscription that it configures, so RDS is no longer what the listener is waiting on. It is
  // still warming, which is what makes this a test of VHDS gating warming rather than of RDS.
  auto config_reload = test_server_->counter("http.config_test.rds.my_route.config_reload");
  ASSERT_NE(nullptr, config_reload);
  EXPECT_EQ(0, config_reload->value());
  EXPECT_FALSE(workersStarted());

  sendInitialVhdsResponse();
  // The route configuration is published exactly once, by the initial VHDS response.
  test_server_->waitForCounter("http.config_test.rds.my_route.config_reload", testing::Eq(1));
  waitForListenerToServe();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "sni.lyft.com");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// An inline route configuration that uses VHDS, in a listener that arrives over LDS after the
// server is already live. This is deliberately a separate fixture from VhdsIntegrationTest, whose
// listener comes from the bootstrap and is therefore built while the server is still initializing.
//
// The distinction matters: the route config provider registers its warming target with the
// *listener's* init manager. Registering it with the server's - which is Initialized once the
// server is live - hits Init::ManagerImpl::add()'s State::Initialized case, which asserts in debug
// builds and, because that case has no return, silently drops the target in release builds so the
// VHDS subscription is never started at all.
class VhdsInlineOnLdsListenerTest : public HttpIntegrationTest,
                                    public Grpc::UnifiedOrLegacyMuxIntegrationParamTest {
public:
  VhdsInlineOnLdsListenerTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(), config()) {
    use_lds_ = false;
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      isUnified() ? "true" : "false");
  }

  void TearDown() override { cleanUpXdsConnection(); }

  // A listener whose HCM carries an inline route configuration that uses VHDS - the shape that the
  // server-init-manager registration used to break.
  envoy::config::listener::v3::Listener listenerWithInlineVhds() {
    return TestUtility::parseYaml<envoy::config::listener::v3::Listener>(fmt::format(
        R"EOF(
name: http
address:
  socket_address:
    address: {}
    port_value: 0
filter_chains:
- filters:
  - name: http
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      stat_prefix: vhds_lds_test
      codec_type: HTTP2
      http_filters:
      - name: envoy.filters.http.router
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      route_config:
        name: my_route
        vhds:
          config_source:
            api_config_source:
              api_type: DELTA_GRPC
              transport_api_version: V3
              grpc_services:
                envoy_grpc:
                  cluster_name: xds_cluster
)EOF",
        Network::Test::getLoopbackAddressString(ipVersion())));
  }

  void initialize() override {
    // Drop the bootstrap listener and serve listeners over LDS from the same xDS cluster instead.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
      auto* lds_config = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
      lds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source = lds_config->mutable_api_config_source();
      api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
      api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("xds_cluster");
    });

    setUpstreamCount(2);
    setUpstreamProtocol(Http::CodecType::HTTP2);
    defer_listener_finalization_ = true;
    HttpIntegrationTest::initialize();

    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();

    // Complete LDS with no listeners at all, so the server finishes initializing and starts its
    // workers. Everything the test does after this happens against a live server whose init
    // manager is Initialized, which is the whole point of the fixture.
    EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TestTypeUrl::get().Listener, "", {}, true));
    sendSotwDiscoveryResponse<envoy::config::listener::v3::Listener>(
        Config::TestTypeUrl::get().Listener, {}, "1");
    test_server_->waitForGauge("listener_manager.workers_started", testing::Eq(1));
  }

  FakeStreamPtr vhds_stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, VhdsInlineOnLdsListenerTest,
                         UNIFIED_LEGACY_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(VhdsInlineOnLdsListenerTest, InlineVhdsOnListenerAddedAfterServerIsLive) {
  initialize();

  // Deliver the listener now that the server is live.
  sendSotwDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TestTypeUrl::get().Listener, {listenerWithInlineVhds()}, "2");

  // Because the workers are already running, a newly added listener goes into warming_listeners_
  // rather than straight into active_listeners_, so unlike the bootstrap case this gauge is a
  // direct read of "the listener is warming".
  test_server_->waitForGauge("listener_manager.total_listeners_warming", testing::Eq(1));

  // The inline route configuration started its VHDS subscription. This is the assertion the old
  // implementation failed: it registered the VHDS init target with the server's init manager,
  // which is Initialized by now, so the target was dropped and start() never ran - meaning this
  // stream would never be created and the wait below would time out.
  AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, vhds_stream_);
  RELEASE_ASSERT(result, result.message());
  vhds_stream_->startGrpcStream();
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  // The virtual hosts haven't arrived, so the listener is still warming and not yet serving.
  EXPECT_EQ(1, test_server_->gauge("listener_manager.total_listeners_warming")->value());
  EXPECT_EQ(0, test_server_->gauge("listener_manager.total_listeners_active")->value());

  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
          fmt::format(VhostTemplate, "my_route/vhost_0", "sni.lyft.com"))},
      {}, "1", vhds_stream_.get());

  // The virtual hosts arrived, so the route configuration goes live and the listener finishes
  // warming.
  test_server_->waitForGauge("listener_manager.total_listeners_warming", testing::Eq(0));
  test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(1));
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // The virtual host that only VHDS knows about is reachable, i.e. the listener didn't come up
  // serving a route configuration that was missing it.
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "sni.lyft.com");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// On-demand VHDS tests with the filter chain add-header-filter -> on_demand -> router.
// add-header-filter stamps x-header-to-add on every decodeHeaders() call, so the upstream
// sees one copy when the stream is continued (guard on) and two copies when it is recreated
// (envoy.reloadable_features.on_demand_vhds_no_recreate_stream set to false).
class OnDemandVhdsRecreateStreamRegressionTest : public VhdsIntegrationTest {
public:
  void initialize() override {
    config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.OnDemand
    )EOF");
    config_helper_.prependFilter(R"EOF(
    name: add-header-filter
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddHeaderEmptyFilterConfig
    )EOF");
    VhdsIntegrationTest::initialize();
  }

  // Sends a header-only request for the unknown vhost.first, resolves it through VHDS and
  // expects a 200. upstream_request_ is left for the caller's assertions.
  void runVhdsOnDemandHeaderOnly() {
    initialize();

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/"},
                                                   {":scheme", "http"},
                                                   {":authority", "vhost.first"},
                                                   {"x-lyft-user-id", "123"}};
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

    // Verify on-demand VHDS discovery is triggered for the unknown vhost.
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                             {vhdsRequestResourceName("vhost.first")}, {},
                                             vhds_stream_.get()));

    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TestTypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "2", vhds_stream_.get(),
        {"my_route/vhost.first"});
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));

    waitForNextUpstreamRequest(1);
    EXPECT_TRUE(upstream_request_->complete());

    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  // Same as runVhdsOnDemandHeaderOnly() with a fully read request body, which must reach the
  // upstream intact.
  void runVhdsOnDemandWithBody() {
    initialize();

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                   {":path", "/"},
                                                   {":scheme", "http"},
                                                   {":authority", "vhost.first"},
                                                   {"x-lyft-user-id", "123"}};
    const std::string request_body(64, 'a');
    IntegrationStreamDecoderPtr response =
        codec_client_->makeRequestWithBody(request_headers, request_body, /*end_stream=*/true);

    // Verify on-demand VHDS discovery is triggered for the unknown vhost.
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                             {vhdsRequestResourceName("vhost.first")}, {},
                                             vhds_stream_.get()));

    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TestTypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "2", vhds_stream_.get(),
        {"my_route/vhost.first"});
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));

    waitForNextUpstreamRequest(1);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_body, upstream_request_->body().toString());

    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  // vhost.first with a single route for /foo: VHDS resolves the host, but /bar matches no route.
  envoy::config::route::v3::VirtualHost buildVirtualHostWithoutMatchingRoute() {
    return TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(R"EOF(
name: my_route/vhost_1
domains: ["vhost.first"]
routes:
- match: { prefix: "/foo" }
  route: { cluster: "my_service" }
)EOF");
  }

  // Sends a header-only request for /bar on the unknown vhost.first, which VHDS resolves to a
  // virtual host without a route for /bar, and expects a 404 without another VHDS request.
  void runVhdsOnDemandNoMatchingRoute() {
    initialize();

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/bar"},
                                                   {":scheme", "http"},
                                                   {":authority", "vhost.first"},
                                                   {"x-lyft-user-id", "123"}};
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                             {vhdsRequestResourceName("vhost.first")}, {},
                                             vhds_stream_.get()));
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TestTypeUrl::get().VirtualHost, {buildVirtualHostWithoutMatchingRoute()}, {}, "2",
        vhds_stream_.get(), {"my_route/vhost.first"});
    // Only the ACK follows, no new request for vhost.first.
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));

    // Waiting for VHDS again would only end the request at the stream idle timeout.
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("404", response->headers().getStatusValue());
    // The stream was neither recreated nor sent through add-header-filter again.
    EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_total")->value());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, OnDemandVhdsRecreateStreamRegressionTest,
                         VHDS_INTEGRATION_PARAMS, vhdsTestParamsToString);

// Request with body, guard on: the decode chain runs once.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest,
       VhdsOnDemandUpdateWithBodyDoesNotRerunDecodeChain) {
  runVhdsOnDemandWithBody();

  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("x-header-to-add")).size(), 1)
      << "add-header-filter ran more than once: the on_demand VHDS path used "
         "recreateStream() instead of continueDecoding().";

  cleanupUpstreamAndDownstream();
}

// Request with body, guard off: the stream is recreated and the decode chain runs twice.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest,
       VhdsOnDemandUpdateWithBodyLegacyRecreateStreamRerunsDecodeChain) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.on_demand_vhds_no_recreate_stream",
                                    "false");

  runVhdsOnDemandWithBody();

  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("x-header-to-add")).size(), 2)
      << "legacy recreateStream() VHDS path should re-run the decode chain, "
         "stamping x-header-to-add a second time.";

  cleanupUpstreamAndDownstream();
}

// Header-only request, guard on: the request is routed to the vhost resolved by VHDS and the
// decode chain runs once.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest,
       VhdsOnDemandUpdateHeaderOnlyDoesNotRerunDecodeChain) {
  runVhdsOnDemandHeaderOnly();

  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("x-header-to-add")).size(), 1)
      << "add-header-filter ran more than once for a header-only request.";

  cleanupUpstreamAndDownstream();
}

// Header-only request, guard off: the stream is recreated and the decode chain runs twice.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest,
       VhdsOnDemandUpdateHeaderOnlyLegacyRecreateStreamRerunsDecodeChain) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.on_demand_vhds_no_recreate_stream",
                                    "false");

  runVhdsOnDemandHeaderOnly();

  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("x-header-to-add")).size(), 2)
      << "legacy recreateStream() VHDS path should re-run the decode chain on a "
         "header-only request.";

  cleanupUpstreamAndDownstream();
}

// A second request for the same vhost is routed without another VHDS request.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest, VhdsOnDemandUpdateSecondRequestUsesPostVhdsRoute) {
  runVhdsOnDemandHeaderOnly();
  ASSERT_TRUE(upstream_request_->complete());

  Http::TestRequestHeaderMapImpl second_request_headers{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "vhost.first"},
                                                        {"x-lyft-user-id", "456"}};
  IntegrationStreamDecoderPtr second_response =
      codec_client_->makeHeaderOnlyRequest(second_request_headers);

  waitForNextUpstreamRequest(1);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("x-header-to-add")).size(), 1)
      << "add-header-filter ran more than once on the second request; the "
         "second request unexpectedly traversed the on_demand stall path.";

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(second_response->waitForEndStream());
  EXPECT_EQ("200", second_response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// VHDS cannot resolve the vhost: the request gets a 404 and no further VHDS request is sent.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest,
       VhdsOnDemandUpdateUnknownVhostReturns404WithoutLoop) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.unknown"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.unknown")}, {},
                                           vhds_stream_.get()));

  notifyAboutAliasResolutionFailure("2", vhds_stream_, {"my_route/vhost.unknown"});

  response->waitForHeaders();
  EXPECT_EQ("404", response->headers().getStatusValue());

  // Only the ACK follows, no new request for vhost.unknown.
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  cleanupUpstreamAndDownstream();
}

// VHDS resolves the vhost, but none of its routes match the request: the request gets a 404
// instead of waiting for VHDS again.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest, VhdsOnDemandUpdateNoMatchingRouteReturns404) {
  runVhdsOnDemandNoMatchingRoute();
  cleanupUpstreamAndDownstream();
}

// Same as above with the legacy recreateStream() path.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest,
       VhdsOnDemandUpdateNoMatchingRouteLegacyRecreateStreamReturns404) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.on_demand_vhds_no_recreate_stream",
                                    "false");
  runVhdsOnDemandNoMatchingRoute();
  cleanupUpstreamAndDownstream();
}

class VhdsWarmingBlocksUnmatchedRequestsIntegrationTest : public Event::TestUsingSimulatedTime,
                                                          public VhdsIntegrationTest {
public:
  using RouteConfigModifier = std::function<void(envoy::config::route::v3::RouteConfiguration&)>;

  void initialize() override {
    useRdsWithVhosts();
    if (routeConfigType() == RouteConfigType::Static) {
      config_helper_.addConfigModifier(
          [&](envoy::extensions::filters::network::http_connection_manager::v3::
                  HttpConnectionManager& hcm) -> void {
            hcm.clear_rds();
            auto* route_config = hcm.mutable_route_config();
            route_config->CopyFrom(warmingBlocksUnmatchedRequestsRdsConfig());
          });
    }

    setUpstreamCount(2);
    setUpstreamProtocol(Http::CodecType::HTTP2);

    defer_listener_finalization_ = true;
    HttpIntegrationTest::initialize();

    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());

    if (routeConfigType() == RouteConfigType::Rds) {
      result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();

      EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "",
                                              {"my_route"}, true));
      sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
          Config::TestTypeUrl::get().RouteConfiguration,
          {warmingBlocksUnmatchedRequestsRdsConfig()}, "1");
    }

    test_server_->waitUntilListenersReady();
    test_server_->notifyingStatsAllocator().waitForCounterFromStringEq(
        "listener_manager.listener_create_success", 1);
    absl::Notification main_thread_done;
    test_server_->server().dispatcher().post([&main_thread_done] { main_thread_done.Notify(); });
    main_thread_done.WaitForNotification();
    ASSERT_TRUE(workersStarted());
    registerTestServerPorts({"http"});
  }

  // Deterministically synchronizes until Envoy's worker and main dispatchers have processed the
  // incoming requests and queued their initial VHDS update callbacks without advancing SimTime.
  void waitForHeldRequests(uint64_t expected_downstream_rq_total) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    test_server_->notifyingStatsAllocator().waitForCounterFromStringEq(
        "http.config_test.downstream_rq_total", expected_downstream_rq_total);
    test_server_->waitForWorkerThreads();
    absl::Notification done;
    test_server_->server().dispatcher().post([&done] { done.Notify(); });
    done.WaitForNotification();
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  envoy::config::route::v3::RouteConfiguration warmingBlocksUnmatchedRequestsRdsConfig() const {
    auto config = rdsConfig();
    config.mutable_vhds()->mutable_warming_blocks_unmatched_requests();
    if (route_config_modifier_ != nullptr) {
      route_config_modifier_(config);
    }
    return config;
  }

  void startVhdsStreamAndExpectInitialWildcardRequest() {
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, vhds_stream_);
    RELEASE_ASSERT(result, result.message());
    vhds_stream_->startGrpcStream();
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));
  }

  std::string vhdsStatPrefix() const {
    return routeConfigType() == RouteConfigType::Rds ? "http.config_test.rds.vhds.my_route."
                                                     : "vhds.my_route.";
  }

  RouteConfigModifier route_config_modifier_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, VhdsWarmingBlocksUnmatchedRequestsIntegrationTest,
                         VHDS_INTEGRATION_PARAMS, vhdsTestParamsToString);

TEST_P(VhdsWarmingBlocksUnmatchedRequestsIntegrationTest,
       ServesStaticImmediatelyAndHoldsUnmatchedUntilVhdsUpdate) {
  initialize();
  startVhdsStreamAndExpectInitialWildcardRequest();

  // 1. Static virtual host matched route is served immediately while VHDS is in flight.
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");

  // 2. Static virtual host unmatched path immediately returns 404 without waiting for VHDS.
  Http::TestRequestHeaderMapImpl unmatched_path_headers{{":method", "GET"},
                                                        {":path", "/unmatched"},
                                                        {":scheme", "http"},
                                                        {":authority", "vhost.rds.first"}};
  auto static_404_response = codec_client_->makeHeaderOnlyRequest(unmatched_path_headers);
  ASSERT_TRUE(static_404_response->waitForEndStream());
  EXPECT_EQ("404", static_404_response->headers().getStatusValue());

  // 3. Concurrent unmatched-vhost requests are held while initial wildcard VHDS is in flight:
  // - One matching request with headers, body, and request trailers.
  // - One unmatched header-only request.
  Http::TestRequestHeaderMapImpl request_headers1{
      {":method", "POST"}, {":path", "/"}, {":scheme", "http"}, {":authority", "sni.lyft.com"}};
  Http::TestRequestTrailerMapImpl request_trailers{{"x-test-trailer", "trailer-value"}};
  auto [encoder1, response1] = codec_client_->startRequest(request_headers1);
  codec_client_->sendData(encoder1, "hello", false);
  codec_client_->sendTrailers(encoder1, request_trailers);

  Http::TestRequestHeaderMapImpl request_headers2{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "unknown.com"}};
  auto response2 = codec_client_->makeHeaderOnlyRequest(request_headers2);

  waitForHeldRequests(4);
  EXPECT_FALSE(response1->complete());
  EXPECT_FALSE(response2->complete());

  // 4. Deliver the initial wildcard VHDS response with sni.lyft.com.
  sendInitialVhdsResponse();

  // 5. Request 1 wakes with buffered body and trailers intact; request 2 wakes to 404 because
  // unknown.com was not added.
  waitForNextUpstreamRequest(1);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("hello", upstream_request_->body().toString());
  ASSERT_NE(nullptr, upstream_request_->trailers());
  EXPECT_THAT(*upstream_request_->trailers(), HeaderMapEqualRef(&request_trailers));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("404", response2->headers().getStatusValue());

  // 6. Subsequent unmatched request immediately receives 404 without waiting.
  Http::TestRequestHeaderMapImpl unknown_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "still-unknown.com"}};
  auto unknown_response = codec_client_->makeHeaderOnlyRequest(unknown_headers);
  ASSERT_TRUE(unknown_response->waitForEndStream());
  EXPECT_EQ("404", unknown_response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

TEST_P(VhdsWarmingBlocksUnmatchedRequestsIntegrationTest,
       EmptyInitialVhdsResponseWakesHeldRequestTo404) {
  initialize();
  startVhdsStreamAndExpectInitialWildcardRequest();

  // 1. Unmatched request is held while initial VHDS is in flight.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "sni.lyft.com"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForHeldRequests(1);
  EXPECT_FALSE(response->complete());

  // 2. Deliver an empty initial wildcard VHDS response (no resources added or removed).
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, {}, {}, "1", vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  // 3. Held request is woken up and completes with 404.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().getStatusValue());

  // 4. Subsequent unmatched request immediately receives 404 without waiting.
  Http::TestRequestHeaderMapImpl unknown_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "unknown.com"}};
  auto unknown_response = codec_client_->makeHeaderOnlyRequest(unknown_headers);
  ASSERT_TRUE(unknown_response->waitForEndStream());
  EXPECT_EQ("404", unknown_response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

TEST_P(VhdsWarmingBlocksUnmatchedRequestsIntegrationTest,
       InitialVhdsFetchTimeoutWakesHeldRequestTo404) {
  const MonotonicTime start_sim_time = timeSystem().monotonicTime();
  route_config_modifier_ = [](envoy::config::route::v3::RouteConfiguration& config) {
    config.mutable_vhds()->mutable_config_source()->mutable_initial_fetch_timeout()->set_seconds(1);
  };
  initialize();
  startVhdsStreamAndExpectInitialWildcardRequest();

  // 1. Static virtual host is served immediately while initial VHDS is in flight.
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");

  // 2. Unmatched request is held until the VHDS initial_fetch_timeout expires.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "sni.lyft.com"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForHeldRequests(2);
  EXPECT_FALSE(response->complete());

  EXPECT_EQ(start_sim_time, timeSystem().monotonicTime());
  timeSystem().advanceTimeWaitImpl(std::chrono::seconds(1));
  test_server_->notifyingStatsAllocator().waitForCounterFromStringEq(
      vhdsStatPrefix() + "init_fetch_timeout", 1);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().getStatusValue());

  // 3. Subsequent unmatched request immediately receives 404 without waiting.
  Http::TestRequestHeaderMapImpl unknown_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "unknown.com"}};
  auto unknown_response = codec_client_->makeHeaderOnlyRequest(unknown_headers);
  ASSERT_TRUE(unknown_response->waitForEndStream());
  EXPECT_EQ("404", unknown_response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

TEST_P(VhdsWarmingBlocksUnmatchedRequestsIntegrationTest,
       RejectedInitialVhdsUpdateWakesHeldRequestTo404) {
  const MonotonicTime start_sim_time = timeSystem().monotonicTime();
  initialize();
  startVhdsStreamAndExpectInitialWildcardRequest();

  // 1. Unmatched request is held while initial VHDS is in flight.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "sni.lyft.com"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForHeldRequests(1);
  EXPECT_FALSE(response->complete());

  // 2. Send an invalid initial VHDS update (duplicate domain) that Envoy rejects and NACKs.
  auto invalid_vhost = TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(R"EOF(
name: my_route/vhost_invalid
domains: ["sni.lyft.com", "sni.lyft.com"]
routes:
- match: { prefix: "/" }
  route: { cluster: "my_service" }
)EOF");
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, {invalid_vhost}, {}, "1", vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(
      Config::TestTypeUrl::get().VirtualHost, {}, {}, vhds_stream_.get(),
      Grpc::Status::WellKnownGrpcStatus::Internal, "Duplicate entry of domain sni.lyft.com"));
  test_server_->notifyingStatsAllocator().waitForCounterFromStringEq(
      vhdsStatPrefix() + "update_rejected", 1);

  // 3. Rejection ends initial VHDS waiting and wakes held request to 404.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().getStatusValue());

  // 4. Subsequent unmatched request immediately receives 404 without waiting.
  Http::TestRequestHeaderMapImpl unknown_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "unknown.com"}};
  auto unknown_response = codec_client_->makeHeaderOnlyRequest(unknown_headers);
  ASSERT_TRUE(unknown_response->waitForEndStream());
  EXPECT_EQ("404", unknown_response->headers().getStatusValue());

  EXPECT_EQ(start_sim_time, timeSystem().monotonicTime());
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

TEST_P(VhdsWarmingBlocksUnmatchedRequestsIntegrationTest,
       CatchAllStaticVirtualHostServesImmediatelyWithoutHolding) {
  route_config_modifier_ = [](envoy::config::route::v3::RouteConfiguration& config) {
    auto* catch_all = config.add_virtual_hosts();
    catch_all->set_name("vhost_catchall");
    catch_all->add_domains("*");
    auto* route = catch_all->add_routes();
    route->mutable_match()->set_prefix("/");
    route->mutable_route()->set_cluster("my_service");
  };
  initialize();
  startVhdsStreamAndExpectInitialWildcardRequest();

  // 1. Because the static catch-all '*' virtual host matches, any host is served immediately while
  // initial VHDS is still in flight.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl static_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "any.example.com"}};
  auto static_response = codec_client_->makeHeaderOnlyRequest(static_headers);
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(static_response->waitForEndStream());
  EXPECT_EQ("200", static_response->headers().getStatusValue());
  EXPECT_TRUE(static_response->headers().get(Http::LowerCaseString("x-vhost-source")).empty());

  // 2. Deliver a dynamic VHDS virtual host for sni.lyft.com and confirm subsequent requests match
  // the dynamic virtual host ahead of the catch-all.
  auto dynamic_vhost = TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(R"EOF(
name: my_route/vhost_0
domains: ["sni.lyft.com"]
response_headers_to_add:
- header:
    key: "x-vhost-source"
    value: "vhds"
routes:
- match: { prefix: "/" }
  route: { cluster: "my_service" }
)EOF");
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TestTypeUrl::get().VirtualHost, {dynamic_vhost}, {}, "1", vhds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  Http::TestRequestHeaderMapImpl dynamic_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "sni.lyft.com"}};
  auto dynamic_response = codec_client_->makeHeaderOnlyRequest(dynamic_headers);
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(dynamic_response->waitForEndStream());
  EXPECT_EQ("200", dynamic_response->headers().getStatusValue());
  const auto source_header =
      dynamic_response->headers().get(Http::LowerCaseString("x-vhost-source"));
  ASSERT_EQ(1, source_header.size());
  EXPECT_EQ("vhds", source_header[0]->value().getStringView());

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

TEST_P(VhdsWarmingBlocksUnmatchedRequestsIntegrationTest,
       StaticVirtualHostUnmatchedPathWithOnDemandFilterReturns404Immediately) {
  config_helper_.prependFilter(R"EOF(
  name: envoy.filters.http.on_demand
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.OnDemand
  )EOF");
  initialize();
  startVhdsStreamAndExpectInitialWildcardRequest();

  // 1. Request matching static virtual host vhost.rds.first with an unmatched path immediately
  // returns 404 without holding in HCM and without triggering an on-demand VHDS request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl unmatched_path_headers{{":method", "GET"},
                                                        {":path", "/unmatched"},
                                                        {":scheme", "http"},
                                                        {":authority", "vhost.rds.first"}};
  auto static_404_response = codec_client_->makeHeaderOnlyRequest(unmatched_path_headers);
  ASSERT_TRUE(static_404_response->waitForEndStream());
  EXPECT_EQ("404", static_404_response->headers().getStatusValue());

  // 2. Unmatched virtual host request is held by HCM until initial VHDS completes, and does not
  // emit an extra on-demand DeltaDiscoveryRequest.
  Http::TestRequestHeaderMapImpl dynamic_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "sni.lyft.com"}};
  auto dynamic_response = codec_client_->makeHeaderOnlyRequest(dynamic_headers);
  waitForHeldRequests(2);
  EXPECT_FALSE(dynamic_response->complete());

  sendInitialVhdsResponse();

  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(dynamic_response->waitForEndStream());
  EXPECT_EQ("200", dynamic_response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

TEST_P(VhdsWarmingBlocksUnmatchedRequestsIntegrationTest,
       CustomVhostHeaderHoldsMatchingAndRejectsMissingHeader) {
  route_config_modifier_ = [](envoy::config::route::v3::RouteConfiguration& config) {
    config.set_vhost_header("x-vhost");
  };
  initialize();
  startVhdsStreamAndExpectInitialWildcardRequest();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // 1. Request without x-vhost returns 404 immediately even if :authority matches a VHDS host.
  Http::TestRequestHeaderMapImpl missing_vhost_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "sni.lyft.com"}};
  auto missing_response = codec_client_->makeHeaderOnlyRequest(missing_vhost_headers);
  ASSERT_TRUE(missing_response->waitForEndStream());
  EXPECT_EQ("404", missing_response->headers().getStatusValue());

  // 2. Request with an empty x-vhost header also returns 404 immediately.
  Http::TestRequestHeaderMapImpl empty_vhost_headers{{":method", "GET"},
                                                     {":path", "/"},
                                                     {":scheme", "http"},
                                                     {":authority", "ignored.lyft.com"},
                                                     {"x-vhost", ""}};
  auto empty_response = codec_client_->makeHeaderOnlyRequest(empty_vhost_headers);
  ASSERT_TRUE(empty_response->waitForEndStream());
  EXPECT_EQ("404", empty_response->headers().getStatusValue());

  // 3. Request with x-vhost pointing to the dynamic host is held even when :authority matches the
  // static virtual host, and resolves once the initial VHDS response arrives.
  Http::TestRequestHeaderMapImpl custom_vhost_headers{{":method", "GET"},
                                                      {":path", "/rdsone"},
                                                      {":scheme", "http"},
                                                      {":authority", "vhost.rds.first"},
                                                      {"x-vhost", "SNI.LYFT.COM"}};
  auto held_response = codec_client_->makeHeaderOnlyRequest(custom_vhost_headers);
  waitForHeldRequests(3);
  EXPECT_FALSE(held_response->complete());

  sendInitialVhdsResponse();

  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(held_response->waitForEndStream());
  EXPECT_EQ("200", held_response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

} // namespace
} // namespace Envoy
