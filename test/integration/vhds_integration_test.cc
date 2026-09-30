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

} // namespace
} // namespace Envoy
