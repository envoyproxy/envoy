#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

const char ClusterName1[] = "cluster_1";
const char ClusterName2[] = "cluster_2";
const int UpstreamIndex1 = 1;
const int UpstreamIndex2 = 2;

class CdsIntegrationTest : public Grpc::DeltaSotwDeferredClustersIntegrationParamTest,
                           public HttpIntegrationTest {
public:
  CdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::discoveredClustersBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
                                        sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw
                                    ? "GRPC"
                                    : "DELTA_GRPC")),
        cluster_creator_(&ConfigHelper::buildStaticCluster) {

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_cluster_manager()->set_enable_deferred_cluster_creation(
          useDeferredCluster());
    });
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw ||
                                       sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta)
                                          ? "true"
                                          : "false");
    use_lds_ = false;
    sotw_or_delta_ = sotwOrDelta();
  }

  void TearDown() override {
    if (!test_skipped_) {
      cleanUpXdsConnection();
    }
  }

  // Overridden to insert this stuff into the initialize() at the very beginning of
  // HttpIntegrationTest::testRouterHeaderOnlyRequestAndResponse().
  void initialize() override {
    use_lds_ = false;
    test_skipped_ = false;
    // Controls how many addFakeUpstream() will happen in
    // BaseIntegrationTest::createUpstreams() (which is part of initialize()).
    // Make sure this number matches the size of the 'clusters' repeated field in the bootstrap
    // config that you use!
    setUpstreamCount(1);                         // the CDS cluster
    setUpstreamProtocol(Http::CodecType::HTTP2); // CDS uses gRPC uses HTTP2.

    // HttpIntegrationTest::initialize() does many things:
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

    // Create the regular (i.e. not an xDS server) upstreams. We create them manually here after
    // initialize() because finalize() expects all fake_upstreams_ to correspond to a static
    // cluster in the bootstrap config - which we don't want since we're testing dynamic CDS!
    addFakeUpstream(upstream_codec_type_);
    addFakeUpstream(upstream_codec_type_);
    cluster1_ = cluster_creator_(ClusterName1,
                                 fake_upstreams_[UpstreamIndex1]->localAddress()->ip()->port(),
                                 Network::Test::getLoopbackAddressString(ipVersion()),
                                 envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
    cluster2_ = cluster_creator_(ClusterName2,
                                 fake_upstreams_[UpstreamIndex2]->localAddress()->ip()->port(),
                                 Network::Test::getLoopbackAddressString(ipVersion()),
                                 envoy::config::cluster::v3::Cluster::ROUND_ROBIN);

    // Let Envoy establish its connection to the CDS server.
    acceptXdsConnection();

    // Do the initial compareDiscoveryRequest / sendDiscoveryResponse for cluster_1.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendClusterDiscoveryResponse({cluster1_}, {cluster1_}, {}, "55");

    // We can continue the test once we're sure that Envoy's ClusterManager has made use of
    // the DiscoveryResponse describing cluster_1 that we sent.
    // 2 because the statically specified CDS server itself counts as a cluster.
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);

    // Wait for our statically specified listener to become ready, and register its port in the
    // test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  void sendClusterDiscoveryResponse(
      const std::vector<envoy::config::cluster::v3::Cluster>& state_of_the_world,
      const std::vector<envoy::config::cluster::v3::Cluster>& added_or_updated,
      const std::vector<std::string>& removed, const std::string& version) {
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, state_of_the_world, added_or_updated, removed, version);
  }

  // Regression test to catch the code declaring a gRPC service method for {SotW,delta}
  // when the user's bootstrap config asks for the other type.
  void verifyGrpcServiceMethod() {
    EXPECT_TRUE(xds_stream_->waitForHeadersComplete());
    Envoy::Http::LowerCaseString path_string(":path");
    std::string expected_method(
        sotwOrDelta() == Grpc::SotwOrDelta::Sotw || sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw
            ? "/envoy.service.cluster.v3.ClusterDiscoveryService/StreamClusters"
            : "/envoy.service.cluster.v3.ClusterDiscoveryService/DeltaClusters");
    EXPECT_EQ(xds_stream_->headers().get(path_string)[0]->value(), expected_method);
  }

  void acceptXdsConnection() {
    AssertionResult result = // xds_connection_ is filled with the new FakeHttpConnection.
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
    verifyGrpcServiceMethod();
  }

  envoy::config::cluster::v3::Cluster cluster1_;
  envoy::config::cluster::v3::Cluster cluster2_;
  // True if we decided not to run the test after all.
  bool test_skipped_{true};
  Http::CodecType upstream_codec_type_{Http::CodecType::HTTP2};
  std::function<envoy::config::cluster::v3::Cluster(
      const std::string&, int, const std::string&,
      const envoy::config::cluster::v3::Cluster::LbPolicy)>
      cluster_creator_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeltaDeferredCluster, CdsIntegrationTest,
    DELTA_SOTW_GRPC_CLIENT_DEFERRED_CLUSTERS_INTEGRATION_PARAMS,
    Grpc::DeltaSotwDeferredClustersIntegrationParamTest::protocolTestParamsToString);

// 1) Envoy starts up with no static clusters (other than the CDS-over-gRPC server).
// 2) Envoy is told of a cluster via CDS.
// 3) We send Envoy a request, which we verify is properly proxied to and served by that cluster.
// 4) Envoy is told that cluster is gone.
// 5) We send Envoy a request, which should 503.
// 6) Envoy is told that the cluster is back.
// 7) We send Envoy a request, which we verify is properly proxied to and served by that cluster.
TEST_P(CdsIntegrationTest, CdsClusterUpDownUp) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  config_helper_.addConfigModifier(configureProxyStatus());
  initialize();

  if (useDeferredCluster()) {
    test_server_->waitForGaugeEq("thread_local_cluster_manager.worker_0.clusters_inflated", 0);
  } else {
    test_server_->waitForGaugeEq("thread_local_cluster_manager.worker_0.clusters_inflated", 2);
  }

  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex1, "/cluster1");
  test_server_->waitForCounterGe("cluster_manager.cluster_added", 1);

  if (useDeferredCluster()) {
    test_server_->waitForGaugeEq("thread_local_cluster_manager.worker_0.clusters_inflated", 1);
  } else {
    EXPECT_EQ(
        test_server_->gauge("thread_local_cluster_manager.worker_0.clusters_inflated")->value(), 2);
  }

  // Tell Envoy that cluster_1 is gone.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "55", {}, {}, {}));
  sendClusterDiscoveryResponse({}, {}, {ClusterName1}, "42");
  // We can continue the test once we're sure that Envoy's ClusterManager has made use of
  // the DiscoveryResponse that says cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  if (useDeferredCluster()) {
    test_server_->waitForGaugeEq("thread_local_cluster_manager.worker_0.clusters_inflated", 0);
  } else {
    test_server_->waitForGaugeEq("thread_local_cluster_manager.worker_0.clusters_inflated", 1);
  }

  // Now that cluster_1 is gone, the listener (with its routing to cluster_1) should 503.
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/cluster1", "", downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(response->headers().getProxyStatusValue(),
            "envoy; error=destination_unavailable; details=\"cluster_not_found; NC\"");

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is back.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "42", {}, {}, {}));
  sendClusterDiscoveryResponse({cluster1_}, {cluster1_}, {}, "413");

  // We can continue the test once we're sure that Envoy's ClusterManager has made use of
  // the DiscoveryResponse describing cluster_1 that we sent. Again, 2 includes CDS server.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
  if (useDeferredCluster()) {
    EXPECT_EQ(
        test_server_->gauge("thread_local_cluster_manager.worker_0.clusters_inflated")->value(), 0);
  } else {
    test_server_->waitForGaugeEq("thread_local_cluster_manager.worker_0.clusters_inflated", 2);
  }

  // Does *not* call our initialize().
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex1, "/cluster1");
  if (useDeferredCluster()) {
    test_server_->waitForGaugeEq("thread_local_cluster_manager.worker_0.clusters_inflated", 1);
  }

  cleanupUpstreamAndDownstream();
}

// Make sure that clusters won't create new connections on teardown.
TEST_P(CdsIntegrationTest, CdsClusterTeardownWhileConnecting) {
  initialize();
  test_server_->waitForCounterGe("cluster_manager.cluster_added", 1);
  test_server_->waitForCounterExists("cluster.cluster_1.upstream_cx_total");
  Stats::CounterSharedPtr cx_counter = test_server_->counter("cluster.cluster_1.upstream_cx_total");
  // Confirm no upstream connection is attempted so far.
  EXPECT_EQ(0, cx_counter->value());

  // Make the upstreams stop working, to ensure the connection was not
  // established.
  fake_upstreams_[1]->dispatcher()->exit();
  fake_upstreams_[2]->dispatcher()->exit();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/cluster1"}, {":scheme", "http"}, {":authority", "host"}});

  // Tell Envoy that cluster_1 is gone.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "55", {}, {}, {}));
  sendClusterDiscoveryResponse({}, {}, {ClusterName1}, "42");
  // We can continue the test once we're sure that Envoy's ClusterManager has made use of
  // the DiscoveryResponse that says cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);
  codec_client_->sendReset(encoder_decoder.first);
  cleanupUpstreamAndDownstream();

  // Either 0 or 1 upstream connection is attempted but no more.
  EXPECT_LE(cx_counter->value(), 1);
}

class DeferredCreationClusterStatsTest : public CdsIntegrationTest {
public:
  void initializeDeferredCreationTest(bool enable_deferred_creation_stats) {
    use_real_stats_ = true;
    config_helper_.addConfigModifier(
        [enable_deferred_creation_stats](::envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          bootstrap.mutable_deferred_stat_options()->set_enable_deferred_creation_stats(
              enable_deferred_creation_stats);
        });
    CdsIntegrationTest::initialize();
    test_server_->waitForCounterGe("cluster_manager.cluster_added", 1);
  }

  void sendRequestToClusterAndWaitForResponse() {
    BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
        lookupPort("http"), "GET", "/cluster1", "", downstream_protocol_, version_, "foo.com");
    ASSERT_TRUE(response->complete());
    cleanupUpstreamAndDownstream();
  };

  void updateCluster() {
    envoy::config::cluster::v3::Cluster cluster1_updated = cluster_creator_(
        ClusterName1, fake_upstreams_[UpstreamIndex2]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()),
        envoy::config::cluster::v3::Cluster::ROUND_ROBIN);

    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {cluster1_updated}, {cluster1_updated}, {}, "42");
  }

  void removeClusters(const std::vector<std::string>& removed) {
    uint64_t cluster_removed = test_server_->counter("cluster_manager.cluster_removed")->value();
    sendClusterDiscoveryResponse({}, {}, removed, "42");
    test_server_->waitForCounterGe("cluster_manager.cluster_removed",
                                   cluster_removed + removed.size());
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDelta, DeferredCreationClusterStatsTest,
    DELTA_SOTW_GRPC_CLIENT_DEFERRED_CLUSTERS_INTEGRATION_PARAMS,
    Grpc::DeltaSotwDeferredClustersIntegrationParamTest::protocolTestParamsToString);

// Test that DeferredCreationTrafficStats gets created and updated correctly.
TEST_P(DeferredCreationClusterStatsTest,
       DeferredCreationTrafficStatsWithClusterCreateUpdateDelete) {
  initializeDeferredCreationTest(/*enable_deferred_creation_stats=*/true);

  EXPECT_EQ(test_server_->gauge("cluster.cluster_1.ClusterTrafficStats.initialized")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total"), nullptr);

  sendRequestToClusterAndWaitForResponse();
  // cluster_1 trafficStats updated.
  EXPECT_EQ(test_server_->gauge("cluster.cluster_1.ClusterTrafficStats.initialized")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);

  updateCluster();
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 2);

  // cluster_1 traffic stats not lost.
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  removeClusters({ClusterName1});
  // update_success is 3: initialize(), update cluster1. and remove cluster1.
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 3);
  sendClusterDiscoveryResponse({cluster2_}, {cluster2_}, {}, "43");
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 4);
  EXPECT_EQ(test_server_->counter("cluster_manager.cluster_added")->value(), 3);
  // Now the cluster_1 stats are gone, as well as the lazy init wrapper.
  test_server_->waitForCounterNonexistent("cluster.cluster_1.upstream_cx_total",
                                          TestUtility::DefaultTimeout);
  EXPECT_EQ(test_server_->gauge("cluster.cluster_1.ClusterTrafficStats.initialized"), nullptr);

  // No cluster_2 traffic stats.
  EXPECT_EQ(test_server_->gauge("cluster.cluster_2.ClusterTrafficStats.initialized")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_2.upstream_cx_total"), nullptr);
}

// Test that Non-DeferredCreationTrafficStats gets created and updated correctly.
TEST_P(DeferredCreationClusterStatsTest,
       NonDeferredCreationTrafficStatsWithClusterCreateUpdateDelete) {
  initializeDeferredCreationTest(/*enable_deferred_creation_stats=*/false);
  // cluster_1 trafficStats created by CDS push.
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 0);

  sendRequestToClusterAndWaitForResponse();
  // cluster_1 trafficStats updated.
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);

  updateCluster();
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 2);

  // cluster_1 traffic stats not lost.
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  removeClusters({ClusterName1});
  // update_success is 3: initialize(), update cluster1. and remove cluster1.
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 3);
  sendClusterDiscoveryResponse({cluster2_}, {cluster2_}, {}, "43");
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 4);
  EXPECT_EQ(test_server_->counter("cluster_manager.cluster_added")->value(), 3);
  // Now the cluster_1 stats are gone.
  test_server_->waitForCounterNonexistent("cluster.cluster_1.upstream_cx_total",
                                          TestUtility::DefaultTimeout);
  // cluster_2 traffic stats stays.
  EXPECT_EQ(test_server_->counter("cluster.cluster_2.upstream_cx_total")->value(), 0);
}

// Test that DeferredCreationTrafficStats with cluster_1 create-remove-create sequence.
TEST_P(DeferredCreationClusterStatsTest,
       DeferredCreationTrafficStatsWithClusterCreateDeleteRecreate) {
  initializeDeferredCreationTest(/*enable_deferred_creation_stats=*/true);
  EXPECT_EQ(test_server_->gauge("cluster.cluster_1.ClusterTrafficStats.initialized")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total"), nullptr);

  sendRequestToClusterAndWaitForResponse();
  // cluster_1 trafficStats updated.
  EXPECT_EQ(test_server_->gauge("cluster.cluster_1.ClusterTrafficStats.initialized")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  removeClusters({ClusterName1});
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 2);
  sendClusterDiscoveryResponse({cluster2_}, {cluster2_}, {}, "43");
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 3);
  EXPECT_EQ(test_server_->counter("cluster_manager.cluster_added")->value(), 3);
  // No cluster_2 traffic stats.
  EXPECT_EQ(test_server_->gauge("cluster.cluster_2.ClusterTrafficStats.initialized")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_2.upstream_cx_total"), nullptr);
  // Now the cluster_1 stats are gone, as well as the lazy init wrapper.
  test_server_->waitForCounterNonexistent("cluster.cluster_1.upstream_cx_total",
                                          TestUtility::DefaultTimeout);
  EXPECT_EQ(test_server_->gauge("cluster.cluster_1.ClusterTrafficStats.initialized"), nullptr);
  // Now add cluster1 back.
  updateCluster();
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 4);
  // Now the cluster_1.ClusterTrafficStats.initialized gauge is 0, since it didn't see previous
  // stats.
  EXPECT_EQ(test_server_->gauge("cluster.cluster_1.ClusterTrafficStats.initialized")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total"), nullptr);

  // cluster_1 traffic stats created, due to the above http request.
  sendRequestToClusterAndWaitForResponse();
  EXPECT_EQ(test_server_->gauge("cluster.cluster_1.ClusterTrafficStats.initialized")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
}

// Test that Non-DeferredCreationTrafficStats with cluster_1 create-remove-create sequence.
TEST_P(DeferredCreationClusterStatsTest,
       NonDeferredCreationTrafficStatsWithClusterCreateDeleteRecreate) {
  initializeDeferredCreationTest(/*enable_deferred_creation_stats=*/false);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 0);

  sendRequestToClusterAndWaitForResponse();
  // cluster_1 trafficStats updated.
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  removeClusters({ClusterName1});
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 2);
  sendClusterDiscoveryResponse({cluster2_}, {cluster2_}, {}, "43");
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 3);
  EXPECT_EQ(test_server_->counter("cluster_manager.cluster_added")->value(), 3);
  // cluster_2 traffic stats created.
  EXPECT_EQ(test_server_->counter("cluster.cluster_2.upstream_cx_total")->value(), 0);
  // Now the cluster_1 stats are gone.
  test_server_->waitForCounterNonexistent("cluster.cluster_1.upstream_cx_total",
                                          TestUtility::DefaultTimeout);
  // Now add cluster1 back.
  updateCluster();
  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 4);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 0);
  sendRequestToClusterAndWaitForResponse();
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
}

// Test the fast addition and removal of clusters when they use ThreadAwareLb.
TEST_P(CdsIntegrationTest, CdsClusterWithThreadAwareLbCycleUpDownUp) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex1, "/cluster1");
  test_server_->waitForCounterGe("cluster_manager.cluster_added", 1);

  // Tell Envoy that cluster_1 is gone.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "55", {}, {}, {}));
  sendClusterDiscoveryResponse({}, {}, {ClusterName1}, "42");
  // Make sure that Envoy's ClusterManager has made use of the DiscoveryResponse that says
  // cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  // Update cluster1_ to use MAGLEV load balancer policy.
  cluster1_ = ConfigHelper::buildStaticCluster(
      ClusterName1, fake_upstreams_[UpstreamIndex1]->localAddress()->ip()->port(),
      Network::Test::getLoopbackAddressString(ipVersion()),
      envoy::config::cluster::v3::Cluster::MAGLEV);

  // Cyclically add and remove cluster with ThreadAwareLb.
  for (int i = 42; i < 142; i += 2) {
    EXPECT_TRUE(
        compareDiscoveryRequest(Config::TypeUrl::get().Cluster, absl::StrCat(i), {}, {}, {}));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {cluster1_}, {cluster1_}, {}, absl::StrCat(i + 1));
    EXPECT_TRUE(
        compareDiscoveryRequest(Config::TypeUrl::get().Cluster, absl::StrCat(i + 1), {}, {}, {}));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {}, {}, {ClusterName1}, absl::StrCat(i + 2));
  }

  cleanupUpstreamAndDownstream();
}

// Tests adding a cluster, adding another, then removing the first.
TEST_P(CdsIntegrationTest, TwoClusters) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex1, "/cluster1");

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_2 is here.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "55", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster2_}, {}, "42");
  // The '3' includes the fake CDS server.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);

  // A request for cluster_2 should be fine.
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex2, "/cluster2");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is gone.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "42", {}, {}, {}));
  sendClusterDiscoveryResponse({cluster2_}, {}, {ClusterName1}, "43");
  // We can continue the test once we're sure that Envoy's ClusterManager has made use of
  // the DiscoveryResponse that says cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  // Even with cluster_1 gone, a request for cluster_2 should be fine.
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex2, "/cluster2");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Tell Envoy that cluster_1 is back.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "43", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster1_}, {}, "413");

  // We can continue the test once we're sure that Envoy's ClusterManager has made use of
  // the DiscoveryResponse describing cluster_1 that we sent. Again, 3 includes CDS server.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);

  // Does *not* call our initialize().
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex1, "/cluster1");

  cleanupUpstreamAndDownstream();
}

// Test internal redirect to a cluster removed during the backend think time.
TEST_P(CdsIntegrationTest, TwoClustersAndRedirects) {
  setDownstreamProtocol(Http::CodecType::HTTP1);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(1);
        route->mutable_route()
            ->mutable_internal_redirect_policy()
            ->mutable_redirect_response_codes()
            ->Add(302);
      });

  // Tell Envoy that cluster_2 is here.
  initialize();
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster2_}, {}, "42");
  // The '3' includes the fake CDS server.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);
  // Tell Envoy that cluster_1 is gone.
  sendClusterDiscoveryResponse({cluster2_}, {}, {ClusterName1}, "43");
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  default_request_headers_.setPath("/cluster2");
  default_request_headers_.setContentLength("4");
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  Buffer::OwnedImpl data("body");
  encoder_decoder.first.encodeData(data, true);
  auto& response = encoder_decoder.second;

  ASSERT_TRUE(fake_upstreams_[UpstreamIndex2]->waitForHttpConnection(*dispatcher_,
                                                                     fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  Http::TestResponseHeaderMapImpl redirect_response{
      {":status", "302"}, {"content-length", "0"}, {"location", "http://host/cluster1"}};

  // Send a response to the original request redirecting to the deleted cluster.
  upstream_request_->encodeHeaders(redirect_response, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Tests that when Envoy's delta xDS stream dis/reconnects, Envoy can inform the server of the
// resources it already has: the reconnected stream need not start with a state-of-the-world
// update.
TEST_P(CdsIntegrationTest, VersionsRememberedAfterReconnect) {
  SKIP_IF_XDS_IS(Grpc::SotwOrDelta::Sotw);
  SKIP_IF_XDS_IS(Grpc::SotwOrDelta::UnifiedSotw);

  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex1, "/cluster1");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Close the connection carrying Envoy's xDS gRPC stream...
  AssertionResult result = xds_connection_->close();
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
  xds_connection_.reset();
  // ...and reconnect it.
  acceptXdsConnection();

  // Upon reconnecting, the Envoy should tell us its current resource versions.
  envoy::service::discovery::v3::DeltaDiscoveryRequest request;
  result = xds_stream_->waitForGrpcMessage(*dispatcher_, request);
  RELEASE_ASSERT(result, result.message());
  const auto& initial_resource_versions = request.initial_resource_versions();
  EXPECT_EQ("55", initial_resource_versions.at(std::string(ClusterName1)));
  EXPECT_EQ(1, initial_resource_versions.size());

  // Tell Envoy that cluster_2 is here. This update does *not* need to include cluster_1,
  // which Envoy should already know about despite the disconnect.
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                                  {cluster2_}, {}, "42");
  // The '3' includes the fake CDS server.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);

  // A request for cluster_1 should be fine.
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex1, "/cluster1");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  // A request for cluster_2 should be fine.
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex2, "/cluster2");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// This test verifies that Envoy can delete a cluster with a lot of idle connections.
// The original problem was recursive closure of idle connections that can run out
// of stack when there are a lot of idle connections.
TEST_P(CdsIntegrationTest, CdsClusterDownWithLotsOfIdleConnections) {
  constexpr int num_requests = 2000;
  // Make upstream H/1 so it creates connection for each request
  upstream_codec_type_ = Http::CodecType::HTTP1;
  // Relax default circuit breaker limits and timeouts so Envoy can accumulate a lot of idle
  // connections
  cluster_creator_ = &ConfigHelper::buildH1ClusterWithHighCircuitBreakersLimits;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->mutable_timeout()
            ->set_seconds(600);
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->mutable_idle_timeout()
            ->set_seconds(600);
      });
  initialize();
  std::vector<IntegrationStreamDecoderPtr> responses;
  std::vector<FakeHttpConnectionPtr> upstream_connections;
  std::vector<FakeStreamPtr> upstream_requests;
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  // The first loop establishes a lot of open connections with active requests to upstream
  for (int i = 0; i < num_requests; ++i) {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/cluster1"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"},
                                                   {"x-lyft-user-id", absl::StrCat(i)}};

    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    responses.push_back(std::move(response));

    FakeHttpConnectionPtr fake_upstream_connection;
    waitForNextUpstreamConnection({UpstreamIndex1}, TestUtility::DefaultTimeout,
                                  fake_upstream_connection);
    // Wait for the next stream on the upstream connection.
    FakeStreamPtr upstream_request;
    AssertionResult result =
        fake_upstream_connection->waitForNewStream(*dispatcher_, upstream_request);
    RELEASE_ASSERT(result, result.message());
    // Wait for the stream to be completely received.
    result = upstream_request->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
    upstream_connections.push_back(std::move(fake_upstream_connection));
    upstream_requests.push_back(std::move(upstream_request));
  }

  // This loop completes all requests making the all upstream connections idle
  for (int i = 0; i < num_requests; ++i) {
    // Send response headers, and end_stream if there is no response body.
    upstream_requests[i]->encodeHeaders(default_response_headers_, true);
    // Wait for the response to be read by the codec client.
    RELEASE_ASSERT(responses[i]->waitForEndStream(), "unexpected timeout");
    ASSERT_TRUE(responses[i]->complete());
    EXPECT_EQ("200", responses[i]->headers().getStatusValue());
  }

  test_server_->waitForCounterGe("cluster_manager.cluster_added", 1);

  // Tell Envoy that cluster_1 is gone. Envoy will try to close all idle connections
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "55", {}, {}, {}));
  sendClusterDiscoveryResponse({}, {}, {ClusterName1}, "42");
  // We can continue the test once we're sure that Envoy's ClusterManager has made use of
  // the DiscoveryResponse that says cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  // If we made it this far then everything is ok.
  for (int i = 0; i < num_requests; ++i) {
    AssertionResult result = upstream_connections[i]->close();
    RELEASE_ASSERT(result, result.message());
    result = upstream_connections[i]->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
  }
  upstream_connections.clear();
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// This test verifies that Envoy can delete a cluster with a lot of connections in the connecting
// state and associated pending requests. The recursion guard in the
// ConnPoolImplBase::closeIdleConnectionsForDrainingPool() would fire if it was called
// recursively.
//
// Test is currently disabled as there is presently no reliable way of making upstream connections
// hang in connecting state.
TEST_P(CdsIntegrationTest, DISABLED_CdsClusterDownWithLotsOfConnectingConnections) {
  // Use low number of pending connections to prevent bumping into the default
  // limit of 128, since the upstream will be prevented below from
  // accepting connections.
  constexpr int num_requests = 64;
  // Make upstream H/1 so it creates connection for each request
  upstream_codec_type_ = Http::CodecType::HTTP1;
  cluster_creator_ = &ConfigHelper::buildH1ClusterWithHighCircuitBreakersLimits;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->mutable_timeout()
            ->set_seconds(600);
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->mutable_idle_timeout()
            ->set_seconds(600);
      });
  initialize();
  test_server_->waitForCounterGe("cluster_manager.cluster_added", 1);
  std::vector<IntegrationStreamDecoderPtr> responses;
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  // Stop upstream at UpstreamIndex1 dispatcher, to prevent it from accepting TCP connections.
  // This will cause Envoy's connections to that upstream hang in the connecting state.
  fake_upstreams_[UpstreamIndex1]->dispatcher()->exit();
  for (int i = 0; i < num_requests; ++i) {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/cluster1"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"},
                                                   {"x-lyft-user-id", absl::StrCat(i)}};

    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    responses.push_back(std::move(response));
  }

  // Wait for Envoy to try to establish all expected connections
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_cx_total", num_requests);

  // Tell Envoy that cluster_1 is gone. Envoy will try to close all pending connections
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "55", {}, {}, {}));
  sendClusterDiscoveryResponse({}, {}, {ClusterName1}, "42");
  // We can continue the test once we're sure that Envoy's ClusterManager has made use of
  // the DiscoveryResponse that says cluster_1 is gone.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);

  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  // If we got here it means that the recursion guard in the
  // ConnPoolImplBase::closeIdleConnectionsForDrainingPool() did not fire, which is what this test
  // validates.
}

} // namespace
} // namespace Envoy
