#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/grpc/status.h"

#include "common/config/protobuf_link_hacks.h"
#include "common/config/version_converter.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/version/version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/ads_integration.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, AdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Validate basic config delivery and upgrade.
TEST_P(AdsIntegrationTest, Basic) {
  initialize();
  testBasicFlow();
}

// Validate basic config delivery and upgrade with RateLimiting.
TEST_P(AdsIntegrationTest, BasicWithRateLimiting) {
  initializeAds(true);
  testBasicFlow();
}

// Validate that we can recover from failures.
TEST_P(AdsIntegrationTest, Failure) {
  initialize();

  // Send initial configuration, failing each xDS once (via a type mismatch), validate we can
  // process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().Cluster, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().Cluster, "", {}, {}, {}, true,
      Grpc::Status::WellKnownGrpcStatus::Internal,
      fmt::format("does not match the message-wide type URL {}", Config::TypeUrl::get().Cluster)));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildCluster("cluster_0")},
      {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {}, {}, true,
                                      Grpc::Status::WellKnownGrpcStatus::Internal,
                                      fmt::format("does not match the message-wide type URL {}",
                                                  Config::TypeUrl::get().ClusterLoadAssignment)));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().Listener, {buildRouteConfig("listener_0", "route_config_0")},
      {buildRouteConfig("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().Listener, "", {}, {}, {}, true,
      Grpc::Status::WellKnownGrpcStatus::Internal,
      fmt::format("does not match the message-wide type URL {}", Config::TypeUrl::get().Listener)));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().RouteConfiguration, {buildListener("route_config_0", "cluster_0")},
      {buildListener("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {}, {}, true,
                                      Grpc::Status::WellKnownGrpcStatus::Internal,
                                      fmt::format("does not match the message-wide type URL {}",
                                                  Config::TypeUrl::get().RouteConfiguration)));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  makeSingleRequest();
}

// Validate that the request with duplicate listeners is rejected.
TEST_P(AdsIntegrationTest, DuplicateWarmingListeners) {
  initialize();

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));

  // Send duplicate listeners and validate that the update is rejected.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener,
      {buildListener("duplicae_listener", "route_config_0"),
       buildListener("duplicae_listener", "route_config_0")},
      {buildListener("duplicae_listener", "route_config_0"),
       buildListener("duplicae_listener", "route_config_0")},
      {}, "1");
  test_server_->waitForCounterGe("listener_manager.lds.update_rejected", 1);
}

// Regression test for the use-after-free crash when processing RDS update (#3953).
TEST_P(AdsIntegrationTest, RdsAfterLdsWithNoRdsChanges) {
  initialize();

  // Send initial configuration.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // Validate that we can process a request.
  makeSingleRequest();

  // Update existing LDS (change stat_prefix).
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0", "rds_crash")},
      {buildListener("listener_0", "route_config_0", "rds_crash")}, {}, "2");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  // Update existing RDS (no changes).
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "2");

  // Validate that we can process a request again
  makeSingleRequest();
}

// Regression test for #11877, validate behavior of EDS updates when a cluster is updated and
// an active cluster is replaced by a newer cluster undergoing warming.
TEST_P(AdsIntegrationTest, CdsEdsReplacementWarming) {
  initialize();
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {buildTlsCluster("cluster_0")},
      {buildTlsCluster("cluster_0")}, {}, "2");
  // Inconsistent SotW and delta behaviors for warming, see
  // https://github.com/envoyproxy/envoy/issues/11477#issuecomment-657855029.
  if (sotw_or_delta_ != Grpc::SotwOrDelta::Delta) {
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                        {"cluster_0"}, {}, {}));
  }
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildTlsClusterLoadAssignment("cluster_0")},
      {buildTlsClusterLoadAssignment("cluster_0")}, {}, "2");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "2", {}, {}, {}, true));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "2",
                                      {"cluster_0"}, {}, {}));
}

// Validate that the request with duplicate clusters in the initial request during server init is
// rejected.
TEST_P(AdsIntegrationTest, DuplicateInitialClusters) {
  initialize();

  // Send initial configuration, failing each xDS once (via a type mismatch), validate we can
  // process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster,
      {buildCluster("duplicate_cluster"), buildCluster("duplicate_cluster")},
      {buildCluster("duplicate_cluster"), buildCluster("duplicate_cluster")}, {}, "1");

  test_server_->waitForCounterGe("cluster_manager.cds.update_rejected", 1);
}

// Validates that removing a redis cluster does not crash Envoy.
// Regression test for issue https://github.com/envoyproxy/envoy/issues/7990.
TEST_P(AdsIntegrationTest, RedisClusterRemoval) {
  initialize();

  // Send initial configuration with a redis cluster and a redis proxy listener.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {buildRedisCluster("redis_cluster")},
      {buildRedisCluster("redis_cluster")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"redis_cluster"}, {"redis_cluster"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("redis_cluster")},
      {buildClusterLoadAssignment("redis_cluster")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildRedisListener("listener_0", "redis_cluster")},
      {buildRedisListener("listener_0", "redis_cluster")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"redis_cluster"}, {}, {}));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));

  // Validate that redis listener is successfully created.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // Now send a CDS update, removing redis cluster added above.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("cluster_2")}, {buildCluster("cluster_2")},
      {"redis_cluster"}, "2");

  // Validate that the cluster is removed successfully.
  test_server_->waitForCounterGe("cluster_manager.cluster_removed", 1);
}

// Validate that the request with duplicate clusters in the subsequent requests (warming clusters)
// is rejected.
TEST_P(AdsIntegrationTest, DuplicateWarmingClusters) {
  initialize();

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Send duplicate warming clusters and validate that the update is rejected.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster,
      {buildCluster("duplicate_cluster"), buildCluster("duplicate_cluster")},
      {buildCluster("duplicate_cluster"), buildCluster("duplicate_cluster")}, {}, "2");
  test_server_->waitForCounterGe("cluster_manager.cds.update_rejected", 1);
}

// Verify CDS is paused during cluster warming.
TEST_P(AdsIntegrationTest, CdsPausedDuringWarming) {
  initialize();

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));

  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Send the first warming cluster.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("warming_cluster_1")},
      {buildCluster("warming_cluster_1")}, {"cluster_0"}, "2");

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"warming_cluster_1"}, {"warming_cluster_1"}, {"cluster_0"}));

  // Send the second warming cluster.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("warming_cluster_2")},
      {buildCluster("warming_cluster_2")}, {}, "3");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 2);
  // We would've got a Cluster discovery request with version 2 here, had the CDS not been paused.

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"warming_cluster_2", "warming_cluster_1"},
                                      {"warming_cluster_2"}, {}));

  // Finish warming the clusters.
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment("warming_cluster_1"),
       buildClusterLoadAssignment("warming_cluster_2")},
      {buildClusterLoadAssignment("warming_cluster_1"),
       buildClusterLoadAssignment("warming_cluster_2")},
      {"cluster_0"}, "2");

  // Validate that clusters are warmed.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);

  // CDS is resumed and EDS response was acknowledged.
  if (sotw_or_delta_ == Grpc::SotwOrDelta::Delta) {
    // Envoy will ACK both Cluster messages. Since they arrived while CDS was paused, they aren't
    // sent until CDS is unpaused. Since version 3 has already arrived by the time the version 2
    // ACK goes out, they're both acknowledging version 3.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "3", {}, {}, {}));
  }
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "3", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "2",
                                      {"warming_cluster_2", "warming_cluster_1"}, {}, {}));
}

// Verify cluster warming is finished only on named EDS response.
TEST_P(AdsIntegrationTest, ClusterWarmingOnNamedResponse) {
  initialize();

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));

  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Send the first warming cluster.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("warming_cluster_1")},
      {buildCluster("warming_cluster_1")}, {"cluster_0"}, "2");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"warming_cluster_1"}, {"warming_cluster_1"}, {"cluster_0"}));

  // Send the second warming cluster.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("warming_cluster_2")},
      {buildCluster("warming_cluster_2")}, {}, "3");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 2);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"warming_cluster_2", "warming_cluster_1"},
                                      {"warming_cluster_2"}, {}));

  // Finish warming the first cluster.
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment("warming_cluster_1")},
      {buildClusterLoadAssignment("warming_cluster_1")}, {}, "2");

  // Envoy will not finish warming of the second cluster because of the missing load assignments
  // i,e. no named EDS response.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  // Disconnect and reconnect the stream.
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->startGrpcStream();

  // Envoy will not finish warming of the second cluster because of the missing load assignments
  // i,e. no named EDS response even after disconnect and reconnect.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  // Finish warming the second cluster.
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment("warming_cluster_2")},
      {buildClusterLoadAssignment("warming_cluster_2")}, {}, "3");

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
}

// Regression test for the use-after-free crash when processing RDS update (#3953).
TEST_P(AdsIntegrationTest, RdsAfterLdsWithRdsChange) {
  initialize();

  // Send initial configuration.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // Validate that we can process a request.
  makeSingleRequest();

  // Update existing LDS (change stat_prefix).
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("cluster_1")}, {buildCluster("cluster_1")},
      {"cluster_0"}, "2");
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_1")},
      {buildClusterLoadAssignment("cluster_1")}, {"cluster_0"}, "2");
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0", "rds_crash")},
      {buildListener("listener_0", "route_config_0", "rds_crash")}, {}, "2");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  // Update existing RDS (migrate traffic to cluster_1).
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_1")},
      {buildRouteConfig("route_config_0", "cluster_1")}, {}, "2");

  // Validate that we can process a request after RDS update
  test_server_->waitForCounterGe("http.ads_test.rds.route_config_0.config_reload", 2);
  makeSingleRequest();
}

// Regression test for the use-after-free crash when a listener awaiting an RDS update is destroyed
// (#6116).
TEST_P(AdsIntegrationTest, RdsAfterLdsInvalidated) {

  initialize();

  // STEP 1: Initial setup
  // ---------------------

  // Initial request for any cluster, respond with cluster_0 version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");

  // Initial request for load assignment for cluster_0, respond with version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  // Request for updates to cluster_0 version 1, no response
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  // Initial request for any listener, respond with listener_0 version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  // Request for updates to load assignment version 1, no response
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));

  // Initial request for route_config_0 (referenced by listener_0), respond with version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

  // Wait for initial listener to be created successfully. Any subsequent listeners will then use
  // the dynamic InitManager (see ListenerImpl::initManager).
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // STEP 2: Listener with dynamic InitManager
  // -----------------------------------------

  // Request for updates to listener_0 version 1, respond with version 2. Under the hood, this
  // registers RdsRouteConfigSubscription's init target with the new ListenerImpl instance.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_1")},
      {buildListener("listener_0", "route_config_1")}, {}, "2");

  // Request for updates to route_config_0 version 1, and initial request for route_config_1
  // (referenced by listener_0), don't respond yet!
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_1", "route_config_0"}, {"route_config_1"},
                                      {}));

  // STEP 3: "New listener, who dis?"
  // --------------------------------

  // Request for updates to listener_0 version 2, respond with version 3 (updated stats prefix).
  // This should blow away the previous ListenerImpl instance, which is still waiting for
  // route_config_1...
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "2", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_1", "omg")},
      {buildListener("listener_0", "route_config_1", "omg")}, {}, "3");

  // Respond to prior request for route_config_1. Under the hood, this invokes
  // RdsRouteConfigSubscription::runInitializeCallbackIfAny, which references the defunct
  // ListenerImpl instance. We should not crash in this event!
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_1", "cluster_0")},
      {buildRouteConfig("route_config_1", "cluster_0")}, {"route_config_0"}, "1");

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
}

class AdsFailIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest,
                               public HttpIntegrationTest {
public:
  AdsFailIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(),
                            ConfigHelper::adsBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ? "GRPC" : "DELTA_GRPC",
                                envoy::config::core::v3::ApiVersion::V2)) {
    create_xds_upstream_ = true;
    use_lds_ = false;
    sotw_or_delta_ = sotwOrDelta();
  }

  void TearDown() override { cleanUpXdsConnection(); }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* grpc_service =
          bootstrap.mutable_dynamic_resources()->mutable_ads_config()->add_grpc_services();
      setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
      auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ads_cluster->set_name("ads_cluster");
    });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, AdsFailIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Validate that we don't crash on failed ADS stream.
TEST_P(AdsFailIntegrationTest, ConnectDisconnect) {
  initialize();
  createXdsConnection();
  ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
  xds_stream_->startGrpcStream();
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);
}

class AdsConfigIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest,
                                 public HttpIntegrationTest {
public:
  AdsConfigIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(),
                            ConfigHelper::adsBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ? "GRPC" : "DELTA_GRPC",
                                envoy::config::core::v3::ApiVersion::V2)) {
    create_xds_upstream_ = true;
    use_lds_ = false;
    sotw_or_delta_ = sotwOrDelta();
  }

  void TearDown() override { cleanUpXdsConnection(); }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* grpc_service =
          bootstrap.mutable_dynamic_resources()->mutable_ads_config()->add_grpc_services();
      setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
      auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ads_cluster->set_name("ads_cluster");

      // Add EDS static Cluster that uses ADS as config Source.
      auto* ads_eds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_eds_cluster->set_name("ads_eds_cluster");
      ads_eds_cluster->set_type(envoy::config::cluster::v3::Cluster::EDS);
      auto* eds_cluster_config = ads_eds_cluster->mutable_eds_cluster_config();
      auto* eds_config = eds_cluster_config->mutable_eds_config();
      eds_config->mutable_ads();
    });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, AdsConfigIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// This is s regression validating that we don't crash on EDS static Cluster that uses ADS.
TEST_P(AdsConfigIntegrationTest, EdsClusterWithAdsConfigSource) {
  initialize();
  createXdsConnection();
  ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
  xds_stream_->startGrpcStream();
  xds_stream_->finishGrpcStream(Grpc::Status::Ok);
}

// Validates that the initial xDS request batches all resources referred to in static config
TEST_P(AdsIntegrationTest, XdsBatching) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_dynamic_resources()->clear_cds_config();
    bootstrap.mutable_dynamic_resources()->clear_lds_config();

    auto static_resources = bootstrap.mutable_static_resources();
    static_resources->add_clusters()->MergeFrom(buildCluster("eds_cluster"));
    static_resources->add_clusters()->MergeFrom(buildCluster("eds_cluster2"));

    static_resources->add_listeners()->MergeFrom(buildListener("rds_listener", "route_config"));
    static_resources->add_listeners()->MergeFrom(buildListener("rds_listener2", "route_config2"));
  });

  on_server_init_function_ = [this]() {
    createXdsConnection();
    ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
    xds_stream_->startGrpcStream();

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                        {"eds_cluster2", "eds_cluster"},
                                        {"eds_cluster2", "eds_cluster"}, {}, true));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment,
        {buildClusterLoadAssignment("eds_cluster"), buildClusterLoadAssignment("eds_cluster2")},
        {buildClusterLoadAssignment("eds_cluster"), buildClusterLoadAssignment("eds_cluster2")}, {},
        "1");

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                        {"route_config2", "route_config"},
                                        {"route_config2", "route_config"}, {}));
    sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
        Config::TypeUrl::get().RouteConfiguration,
        {buildRouteConfig("route_config2", "eds_cluster2"),
         buildRouteConfig("route_config", "dummy_cluster")},
        {buildRouteConfig("route_config2", "eds_cluster2"),
         buildRouteConfig("route_config", "dummy_cluster")},
        {}, "1");
  };

  initialize();
}

// Validates that listeners can be removed before server start.
TEST_P(AdsIntegrationTest, ListenerDrainBeforeServerStart) {
  initialize();

  // Initial request for cluster, response for cluster_0.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");

  // Initial request for load assignment for cluster_0, respond with version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");
  // Request for updates to cluster_0 version 1, no response
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  // Initial request for any listener, respond with listener_0 version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  // Request for updates to load assignment version 1, no response
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));

  // Initial request for route_config_0 (referenced by listener_0), respond with version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));

  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);
  // Before server is started, even though listeners are added to active list
  // we mark them as "warming" in config dump since they're not initialized yet.
  ASSERT_EQ(getListenersConfigDump().dynamic_listeners().size(), 1);
  EXPECT_TRUE(getListenersConfigDump().dynamic_listeners(0).has_warming_state());

  // Remove listener.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(Config::TypeUrl::get().Listener, {},
                                                               {}, {"listener_0"}, "2");
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 0);
}

// Validate that Node message is well formed.
TEST_P(AdsIntegrationTest, NodeMessage) {
  initialize();
  API_NO_BOOST(envoy::api::v2::DiscoveryRequest) sotw_request;
  API_NO_BOOST(envoy::api::v2::DeltaDiscoveryRequest) delta_request;
  const envoy::api::v2::core::Node* node = nullptr;
  if (sotw_or_delta_ == Grpc::SotwOrDelta::Sotw) {
    EXPECT_TRUE(xds_stream_->waitForGrpcMessage(*dispatcher_, sotw_request));
    EXPECT_TRUE(sotw_request.has_node());
    node = &sotw_request.node();
  } else {
    EXPECT_TRUE(xds_stream_->waitForGrpcMessage(*dispatcher_, delta_request));
    EXPECT_TRUE(delta_request.has_node());
    node = &delta_request.node();
  }
  envoy::config::core::v3::BuildVersion build_version_msg;
  Config::VersionConverter::upgrade(node->user_agent_build_version(), build_version_msg);
  EXPECT_THAT(build_version_msg, ProtoEq(VersionInfo::buildVersion()));
  EXPECT_GE(node->extensions().size(), 0);
  EXPECT_EQ(0, node->client_features().size());
  xds_stream_->finishGrpcStream(Grpc::Status::Ok);
}

// Check if EDS cluster defined in file is loaded before ADS request and used as xDS server
class AdsClusterFromFileIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest,
                                          public HttpIntegrationTest {
public:
  AdsClusterFromFileIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(),
                            ConfigHelper::adsBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ? "GRPC" : "DELTA_GRPC",
                                envoy::config::core::v3::ApiVersion::V2)) {
    create_xds_upstream_ = true;
    use_lds_ = false;
    sotw_or_delta_ = sotwOrDelta();
  }

  void TearDown() override { cleanUpXdsConnection(); }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* grpc_service =
          bootstrap.mutable_dynamic_resources()->mutable_ads_config()->add_grpc_services();
      setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
      // Define ADS cluster
      auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_cluster->set_name("ads_cluster");
      ads_cluster->mutable_http2_protocol_options();
      ads_cluster->set_type(envoy::config::cluster::v3::Cluster::EDS);
      auto* ads_cluster_config = ads_cluster->mutable_eds_cluster_config();
      auto* ads_cluster_eds_config = ads_cluster_config->mutable_eds_config();
      // Define port of ADS cluster
      TestEnvironment::PortMap port_map_;
      port_map_["upstream_0"] = xds_upstream_->localAddress()->ip()->port();
      // Path to EDS for ads_cluster
      const std::string eds_path = TestEnvironment::temporaryFileSubstitute(
          "test/config/integration/server_xds.eds.ads_cluster.yaml", port_map_, version_);
      ads_cluster_eds_config->set_path(eds_path);

      // Add EDS static Cluster that uses ADS as config Source.
      auto* ads_eds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_eds_cluster->set_name("ads_eds_cluster");
      ads_eds_cluster->set_type(envoy::config::cluster::v3::Cluster::EDS);
      auto* eds_cluster_config = ads_eds_cluster->mutable_eds_cluster_config();
      auto* eds_config = eds_cluster_config->mutable_eds_config();
      eds_config->mutable_ads();
    });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    HttpIntegrationTest::initialize();
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name) {
    return TestUtility::parseYaml<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        fmt::format(R"EOF(
      cluster_name: {}
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: {}
                port_value: {}
    )EOF",
                    name, Network::Test::getLoopbackAddressString(ipVersion()),
                    fake_upstreams_[0]->localAddress()->ip()->port()));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, AdsClusterFromFileIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Validate if ADS cluster defined as EDS will be loaded from file and connection with ADS cluster
// will be established.
TEST_P(AdsClusterFromFileIntegrationTest, BasicTestWidsAdsEndpointLoadedFromFile) {
  initialize();
  createXdsConnection();
  ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
  xds_stream_->startGrpcStream();

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"ads_eds_cluster"}, {"ads_eds_cluster"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("ads_eds_cluster")},
      {buildClusterLoadAssignment("ads_eds_cluster")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"ads_eds_cluster"}, {}, {}));
}

class AdsIntegrationTestWithRtds : public AdsIntegrationTest {
public:
  AdsIntegrationTestWithRtds() = default;

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* layered_runtime = bootstrap.mutable_layered_runtime();
      auto* layer = layered_runtime->add_layers();
      layer->set_name("foobar");
      auto* rtds_layer = layer->mutable_rtds_layer();
      rtds_layer->set_name("ads_rtds_layer");
      auto* rtds_config = rtds_layer->mutable_rtds_config();
      rtds_config->mutable_ads();

      auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
      ads_config->set_set_node_on_first_message_only(true);
    });
    AdsIntegrationTest::initialize();
  }

  void testBasicFlow() {
    // Test that runtime discovery request comes first and cluster discovery request comes after
    // runtime was loaded.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"ads_rtds_layer"},
                                        {"ads_rtds_layer"}, {}, true));
    auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
      name: ads_rtds_layer
      layer:
        foo: bar
        baz: meh
    )EOF");
    sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
        Config::TypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");

    test_server_->waitForCounterGe("runtime.load_success", 1);
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, false));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "1", {"ads_rtds_layer"}, {},
                                        {}, false));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, AdsIntegrationTestWithRtds,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(AdsIntegrationTestWithRtds, Basic) {
  initialize();
  testBasicFlow();
}

class AdsIntegrationTestWithRtdsAndSecondaryClusters : public AdsIntegrationTestWithRtds {
public:
  AdsIntegrationTestWithRtdsAndSecondaryClusters() = default;

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add secondary cluster to the list of static resources.
      auto* eds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      eds_cluster->set_name("eds_cluster");
      eds_cluster->set_type(envoy::config::cluster::v3::Cluster::EDS);
      auto* eds_cluster_config = eds_cluster->mutable_eds_cluster_config();
      eds_cluster_config->mutable_eds_config()->mutable_ads();
    });
    AdsIntegrationTestWithRtds::initialize();
  }

  void testBasicFlow() {
    // Test that runtime discovery request comes first followed by the cluster load assignment
    // discovery request for secondary cluster and then CDS discovery request.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"ads_rtds_layer"},
                                        {"ads_rtds_layer"}, {}, true));
    auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
      name: ads_rtds_layer
      layer:
        foo: bar
        baz: meh
    )EOF");
    sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
        Config::TypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");

    test_server_->waitForCounterGe("runtime.load_success", 1);
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                        {"eds_cluster"}, {"eds_cluster"}, {}, false));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("eds_cluster")},
        {buildClusterLoadAssignment("eds_cluster")}, {}, "1");

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "1", {"ads_rtds_layer"}, {},
                                        {}, false));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, false));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {buildCluster("cluster_0")}, {buildCluster("cluster_0")},
        {}, "1");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, AdsIntegrationTestWithRtdsAndSecondaryClusters,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(AdsIntegrationTestWithRtdsAndSecondaryClusters, Basic) {
  initialize();
  testBasicFlow();
}

// Check if EDS cluster defined in file is loaded before ADS request and used as xDS server
class AdsClusterV3Test : public AdsIntegrationTest {
public:
  AdsClusterV3Test() : AdsIntegrationTest(envoy::config::core::v3::ApiVersion::V3) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, AdsClusterV3Test,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Verify CDS is paused during cluster warming.
TEST_P(AdsClusterV3Test, CdsPausedDuringWarming) {
  initialize();

  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V3);
  const auto eds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      envoy::config::core::v3::ApiVersion::V3);
  const auto lds_type_url = Config::getTypeUrl<envoy::config::listener::v3::Listener>(
      envoy::config::core::v3::ApiVersion::V3);
  const auto rds_type_url = Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>(
      envoy::config::core::v3::ApiVersion::V3);

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("cluster_0")}, {buildCluster("cluster_0")}, {}, "1", false);
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {"cluster_0"}, {"cluster_0"}, {}));

  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1", false);

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(lds_type_url, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      lds_type_url, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1", false);

  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "1", {"cluster_0"}, {}, {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(rds_type_url, "", {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      rds_type_url, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1", false);

  EXPECT_TRUE(compareDiscoveryRequest(lds_type_url, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(rds_type_url, "1", {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Send the first warming cluster.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("warming_cluster_1")}, {buildCluster("warming_cluster_1")},
      {"cluster_0"}, "2", false);

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "1", {"warming_cluster_1"},
                                      {"warming_cluster_1"}, {"cluster_0"}));

  // Send the second warming cluster.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("warming_cluster_2")}, {buildCluster("warming_cluster_2")}, {},
      "3", false);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 2);
  // We would've got a Cluster discovery request with version 2 here, had the CDS not been paused.

  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "1", {"warming_cluster_2", "warming_cluster_1"},
                                      {"warming_cluster_2"}, {}));

  // Finish warming the clusters.
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url,
      {buildClusterLoadAssignment("warming_cluster_1"),
       buildClusterLoadAssignment("warming_cluster_2")},
      {buildClusterLoadAssignment("warming_cluster_1"),
       buildClusterLoadAssignment("warming_cluster_2")},
      {"cluster_0"}, "2", false);

  // Validate that clusters are warmed.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);

  // CDS is resumed and EDS response was acknowledged.
  if (sotw_or_delta_ == Grpc::SotwOrDelta::Delta) {
    // Envoy will ACK both Cluster messages. Since they arrived while CDS was paused, they aren't
    // sent until CDS is unpaused. Since version 3 has already arrived by the time the version 2
    // ACK goes out, they're both acknowledging version 3.
    EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "3", {}, {}, {}));
  }
  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "3", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "2", {"warming_cluster_2", "warming_cluster_1"},
                                      {}, {}));
}

// Validates that the initial xDS request batches all resources referred to in static config
TEST_P(AdsClusterV3Test, XdsBatching) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_dynamic_resources()->clear_cds_config();
    bootstrap.mutable_dynamic_resources()->clear_lds_config();

    auto static_resources = bootstrap.mutable_static_resources();
    static_resources->add_clusters()->MergeFrom(buildCluster("eds_cluster"));
    static_resources->add_clusters()->MergeFrom(buildCluster("eds_cluster2"));

    static_resources->add_listeners()->MergeFrom(buildListener("rds_listener", "route_config"));
    static_resources->add_listeners()->MergeFrom(buildListener("rds_listener2", "route_config2"));
  });

  on_server_init_function_ = [this]() {
    createXdsConnection();
    ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
    xds_stream_->startGrpcStream();

    const auto eds_type_url =
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
            envoy::config::core::v3::ApiVersion::V3);
    const auto rds_type_url = Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>(
        envoy::config::core::v3::ApiVersion::V3);

    EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {"eds_cluster2", "eds_cluster"},
                                        {"eds_cluster2", "eds_cluster"}, {}, true));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        eds_type_url,
        {buildClusterLoadAssignment("eds_cluster"), buildClusterLoadAssignment("eds_cluster2")},
        {buildClusterLoadAssignment("eds_cluster"), buildClusterLoadAssignment("eds_cluster2")}, {},
        "1", false);

    EXPECT_TRUE(compareDiscoveryRequest(rds_type_url, "", {"route_config2", "route_config"},
                                        {"route_config2", "route_config"}, {}));
    sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
        rds_type_url,
        {buildRouteConfig("route_config2", "eds_cluster2"),
         buildRouteConfig("route_config", "dummy_cluster")},
        {buildRouteConfig("route_config2", "eds_cluster2"),
         buildRouteConfig("route_config", "dummy_cluster")},
        {}, "1", false);
  };

  initialize();
}

} // namespace Envoy
