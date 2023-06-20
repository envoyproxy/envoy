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

    EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                            {"my_route"}, true));
    sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
        Config::TypeUrl::get().RouteConfiguration,
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
      Config::TypeUrl::get().RouteConfiguration,
      {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(RdsConfigWithVhosts)},
      "2");

  auto result = xds_connection_->waitForNewStream(*dispatcher_, vhds_stream_);
  RELEASE_ASSERT(result, result.message());
  vhds_stream_->startGrpcStream();

  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
          fmt::format(VhostTemplate, "my_route/vhost_0", "vhost.first"))},
      {}, "1", vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  // Confirm vhost.first that was configured via VHDS is reachable
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "vhost.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, VhdsIntegrationTest,
                         UNIFIED_LEGACY_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(VhdsIntegrationTest, RdsUpdateWithoutVHDSChangesDoesNotRestartVHDS) {
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "sni.lyft.com");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Update RouteConfig, but don't change VHDS config
  sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration,
      {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(RdsConfigWithVhosts)},
      "2");

  // Confirm vhost_0 that was originally configured via VHDS is reachable
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "sni.lyft.com");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

} // namespace
} // namespace Envoy
