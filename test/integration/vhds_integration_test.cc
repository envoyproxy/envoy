#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/scope.h"

#include "source/common/buffer/buffer_impl.h"
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

// ---------------------------------------------------------------------------
// Regression tests for the VHDS on-demand recreate-stream crash.
//
// OnDemandRouteUpdate::onRouteConfigUpdateCompletion (in
// source/extensions/filters/http/on_demand/on_demand_update.cc) historically
// called callbacks_->recreateStream(nullptr) when VHDS successfully resolved
// the host and the request body had been fully read. That recreate path ran
// ActiveStream::recreateStream at source/common/http/conn_manager_impl.cc
// which unconditionally dereferences filter_manager_.bufferedRequestData()
// via Buffer::OwnedImpl::move -- the same null-dereference signature as the ODCDS
// crash fixed by #43163 for the cluster-discovery path.
//
// The runtime guard envoy.reloadable_features.on_demand_vhds_no_recreate_stream
// (default on) replaces recreateStream with continueDecoding in the VHDS
// path, mirroring envoy.reloadable_features.on_demand_cluster_no_recreate_stream
// introduced by #43163 for ODCDS. See the ``behavior_changes`` release-note
// fragment for the full rationale.
//
// The tests below prepend a trivial add-header-filter *before* the
// envoy.filters.http.on_demand filter so we can tell, at the upstream,
// whether the decode filter chain was invoked once (no recreate, guard on)
// or twice (recreate, guard off). The default test exercises the fix; the
// sibling test explicitly flips the guard off to preserve coverage of the
// legacy recreate path.
// ---------------------------------------------------------------------------

// Reuses VhdsIntegrationTest but prepends on_demand + add-header-filter to
// the HCM filter chain. ``prependFilter`` inserts at the front of the list,
// so calling it with ``add-header-filter`` *after* ``on_demand`` leaves the
// final chain as: add-header-filter -> on_demand -> router.
class OnDemandVhdsRecreateStreamRegressionTest : public VhdsIntegrationTest {
public:
  void initialize() override {
    // on_demand first (ends up second in the chain after the add-header
    // prepend below).
    config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.OnDemand
    )EOF");
    // add-header-filter last: ends up at the head of the chain, before
    // on_demand. It stamps ``x-header-to-add: value`` on every decodeHeaders
    // invocation, which lets us count how many times the pre-on-demand
    // portion of the chain was executed.
    config_helper_.prependFilter(R"EOF(
    name: add-header-filter
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddHeaderEmptyFilterConfig
    )EOF");
    VhdsIntegrationTest::initialize();
  }

  // Shared test-body helper: drives an on-demand VHDS update for a
  // header-only request (no body). Leaves ``upstream_request_`` around for
  // the caller to make its own count-of-x-header assertion. Symmetric with
  // ``runVhdsOnDemandWithBody`` for the body-bearing case so the two
  // header-only parameterized tests (guard-on and guard-off) share the xDS
  // choreography and differ only on their count expectation. This is the
  // shape that closes the 404 regression introduced by PR #1144: even
  // without a body, the pre-fix on-demand path 404s because the
  // ``cached_route_`` is an engaged-null and ``snapped_route_config_``
  // points at the pre-VHDS RouteConfiguration.
  void runVhdsOnDemandHeaderOnly() {
    // Establishes the RDS/VHDS streams and a default vhost of
    // ``sni.lyft.com``. ``vhost.first`` is not yet known and will trigger
    // VHDS on-demand discovery.
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

    // VHDS responds with the host resolved.
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

  // Shared test-body helper: drives an on-demand VHDS update that requires a
  // body. Leaves ``upstream_request_`` around for the caller to make its own
  // count-of-x-header assertion. Keeping the body in a helper lets the two
  // parameterized tests (guard-on and guard-off) share the xDS
  // choreography and differ only on their count expectation.
  void runVhdsOnDemandWithBody() {
    // Establishes the RDS/VHDS streams and a default vhost of
    // ``sni.lyft.com`` (see VhdsIntegrationTest::initialize()).
    // ``vhost.first`` is *not* known yet and will trigger VHDS on-demand
    // discovery.
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

    // VHDS responds with the host resolved -- this is the trigger that fires
    // ``OnDemandRouteUpdate::onRouteConfigUpdateCompletion(true)`` on the
    // worker via ``dispatcher_.post(...)`` from
    // ``source/common/router/rds_impl.cc:154``.
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TestTypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "2", vhds_stream_.get(),
        {"my_route/vhost.first"});
    // Wait for the VHDS ACK so we know the response has been applied.
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));

    // Downstream should complete normally regardless of whether recreate or
    // continue was taken -- the point of this test is not that it crashes
    // (it doesn't, in isolation), but that the decode filter chain runs
    // either once or twice depending on the guard.
    waitForNextUpstreamRequest(1);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_body, upstream_request_->body().toString());

    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, OnDemandVhdsRecreateStreamRegressionTest,
                         VHDS_INTEGRATION_PARAMS, vhdsTestParamsToString);

// Guard-on (default) regression for a body-bearing on-demand VHDS update: the
// resolution must resume via continueDecoding() rather than recreateStream(), so
// the decode chain runs once. The add-header-filter prepended before on_demand
// stamps x-header-to-add on every decodeHeaders(), so a second stamp (size == 2)
// would reveal a chain re-run. Mirrors OnDemandClusterDiscoveryWorksWithNoRecreateStream
// in test/extensions/filters/http/on_demand/odcds_integration_test.cc (#43163).
TEST_P(OnDemandVhdsRecreateStreamRegressionTest,
       VhdsOnDemandUpdateWithBodyDoesNotRerunDecodeChain) {
  runVhdsOnDemandWithBody();

  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("x-header-to-add")).size(), 1)
      << "add-header-filter ran more than once: the on_demand VHDS path used "
         "recreateStream() instead of continueDecoding().";

  cleanupUpstreamAndDownstream();
}

// Symmetric legacy-path coverage: with the guard flipped OFF, the VHDS update
// takes the recreateStream() branch and re-runs the decode chain, so
// add-header-filter stamps x-header-to-add twice (size == 2). Guards the
// rollback escape-hatch.
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

// Guard-on header-only variant: exercises the refreshRouteConfigSnapshot() +
// clearRouteCache() sequence independently of the body-buffer mechanics.
// Reverting either the on_demand_update.cc or conn_manager_impl.cc snapshot
// refresh makes the resumed route() lookup use the pre-VHDS snapshot and 404.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest,
       VhdsOnDemandUpdateHeaderOnlyDoesNotRerunDecodeChain) {
  runVhdsOnDemandHeaderOnly();

  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("x-header-to-add")).size(), 1)
      << "add-header-filter ran more than once for a header-only request.";

  cleanupUpstreamAndDownstream();
}

// Symmetric legacy (guard-off) counterpart of the header-only variant above.
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

// After VHDS resolves the vhost, a second request for the now-known host must
// succeed without another VHDS round-trip -- verifying the shared route provider
// (not just per-stream snapshot state) was updated.
TEST_P(OnDemandVhdsRecreateStreamRegressionTest, VhdsOnDemandUpdateSecondRequestUsesPostVhdsRoute) {
  runVhdsOnDemandHeaderOnly();
  ASSERT_TRUE(upstream_request_->complete());

  // Issue a second request on the same downstream codec_client_. It must
  // resolve directly via the now-populated route table without another VHDS
  // round-trip -- if the provider were not updated, the on-demand filter
  // would park the request waiting for VHDS (the test would time out) or
  // the request would 404. Either way the failure is observable here.
  Http::TestRequestHeaderMapImpl second_request_headers{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "vhost.first"},
                                                        {"x-lyft-user-id", "456"}};
  IntegrationStreamDecoderPtr second_response =
      codec_client_->makeHeaderOnlyRequest(second_request_headers);

  // Reuse the existing fake upstream connection; ``waitForNextUpstreamRequest``
  // accepts new streams on the existing connection or accepts a new
  // connection, whichever the upstream pool decides.
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

// Legitimate-404 path: when VHDS reports the alias unresolvable (route_exists ==
// false), the request must 404 and the filter must NOT clear the route cache --
// otherwise a fresh route() miss would re-post requestRouteConfigUpdate and loop.
// Asserting exactly one VHDS DiscoveryRequest (no follow-up) verifies no loop.
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

  // Exactly one VHDS request goes out for the unknown alias.
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.unknown")}, {},
                                           vhds_stream_.get()));

  // VHDS replies that the alias cannot be resolved.
  notifyAboutAliasResolutionFailure("2", vhds_stream_, {"my_route/vhost.unknown"});

  response->waitForHeaders();
  EXPECT_EQ("404", response->headers().getStatusValue());

  // No second VHDS request should arrive for the same alias: if the fix
  // ever clears the cached null on the route_exists=false path, the next
  // route() call falls through to the on_demand filter again and a new
  // requestRouteConfigUpdate is posted. compareDeltaDiscoveryRequest with
  // an empty resource list verifies the stream is quiescent (no pending
  // request, just the ACK for the failure response above).
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().VirtualHost, {}, {},
                                           vhds_stream_.get()));

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
