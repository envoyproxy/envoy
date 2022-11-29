#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/config/validators/minimum_clusters/v3/minimum_clusters.pb.h"
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

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

const uint32_t InitialUpstreamIndex = 1;

class MinimumClustersValidatorIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest,
                                                public HttpIntegrationTest {
public:
  MinimumClustersValidatorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::discoveredClustersBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
                                        sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw
                                    ? "GRPC"
                                    : "DELTA_GRPC")) {
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
  void initializeTest(uint32_t initial_clusters_num) {
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
    for (uint32_t i = 0; i < initial_clusters_num; ++i) {
      addFakeUpstream(Http::CodecType::HTTP2);
      const std::string cluster_name = absl::StrCat("cluster_", i);
      auto cluster = ConfigHelper::buildStaticCluster(
          cluster_name, fake_upstreams_[InitialUpstreamIndex + i]->localAddress()->ip()->port(),
          Network::Test::getLoopbackAddressString(ipVersion()));
      clusters_.emplace_back(cluster);
    }

    // Let Envoy establish its connection to the CDS server.
    acceptXdsConnection();

    // Do the initial compareDiscoveryRequest / sendDiscoveryResponse for the clusters.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                               clusters_, clusters_, {}, "7");

    // We can continue the test once we're sure that Envoy's ClusterManager has made use of
    // the DiscoveryResponse describing cluster_1 that we sent.
    // 2 because the statically specified CDS server itself counts as a cluster.
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", initial_clusters_num + 1);

    // Wait for our statically specified listener to become ready, and register its port in the
    // test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
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

  void addValidator(uint32_t threshold) {
    config_helper_.addConfigModifier([threshold](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* config_validator_config = bootstrap.mutable_dynamic_resources()
                                          ->mutable_cds_config()
                                          ->mutable_api_config_source()
                                          ->add_config_validators();
      envoy::extensions::config::validators::minimum_clusters::v3::MinimumClustersValidator config;
      config.set_min_clusters_num(threshold);
      config_validator_config->mutable_typed_config()->PackFrom(config);
      config_validator_config->set_name("minimum_cluster_validator");
    });
  }

  std::vector<envoy::config::cluster::v3::Cluster> clusters_;
  // True if we decided not to run the test after all.
  bool test_skipped_{true};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, MinimumClustersValidatorIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(MinimumClustersValidatorIntegrationTest, RemoveAllClustersThreshold0) {
  addValidator(0);
  initializeTest(2);

  // 3 clusters: xds + 2 upstream clusters.
  EXPECT_EQ(3, test_server_->gauge("cluster_manager.active_clusters")->value());

  // Removing the 2 clusters should be successful.
  std::vector<std::string> removed_clusters_names;
  std::transform(clusters_.cbegin(), clusters_.cend(), std::back_inserter(removed_clusters_names),
                 [](const envoy::config::cluster::v3::Cluster& cluster) -> std::string {
                   return cluster.name();
                 });

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "7", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster, {}, {},
                                                             {removed_clusters_names}, "8");

  // Receive ACK.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "8", {}, {}, {}));
  EXPECT_EQ(1, test_server_->gauge("cluster_manager.active_clusters")->value());
}

TEST_P(MinimumClustersValidatorIntegrationTest, RemoveAllClustersThreshold1) {
  addValidator(1);
  initializeTest(2);

  // 3 clusters: xds + 2 upstream clusters.
  EXPECT_EQ(3, test_server_->gauge("cluster_manager.active_clusters")->value());

  // Removing the 2 clusters should be successful.
  std::vector<std::string> removed_clusters_names;
  std::transform(clusters_.cbegin(), clusters_.cend(), std::back_inserter(removed_clusters_names),
                 [](const envoy::config::cluster::v3::Cluster& cluster) -> std::string {
                   return cluster.name();
                 });

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "7", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster, {}, {},
                                                             {removed_clusters_names}, "8");

  // Receive NACK.
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "7", {}, {}, {}, false,
                              Grpc::Status::WellKnownGrpcStatus::Internal,
                              "CDS update attempts to reduce clusters below configured minimum."));
  EXPECT_EQ(3, test_server_->gauge("cluster_manager.active_clusters")->value());
}

} // namespace
} // namespace Envoy
