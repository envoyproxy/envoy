#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v2/ads.pb.h"

#include "common/config/protobuf_link_hacks.h"
#include "common/config/resources.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/ads_integration.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::IsSubstring;

namespace Envoy {

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, AdsIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

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
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().Cluster, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().Cluster, "", {}, {}, {}, Grpc::Status::GrpcStatus::Internal,
      fmt::format("{} does not match {}", Config::TypeUrl::get().ClusterLoadAssignment,
                  Config::TypeUrl::get().Cluster)));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().ClusterLoadAssignment,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "", {"cluster_0"}, {},
                              {}, Grpc::Status::GrpcStatus::Internal,
                              fmt::format("{} does not match {}", Config::TypeUrl::get().Cluster,
                                          Config::TypeUrl::get().ClusterLoadAssignment)));
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().Listener, {buildRouteConfig("listener_0", "route_config_0")},
      {buildRouteConfig("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().Listener, "", {}, {}, {}, Grpc::Status::GrpcStatus::Internal,
      fmt::format("{} does not match {}", Config::TypeUrl::get().RouteConfiguration,
                  Config::TypeUrl::get().Listener)));
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().RouteConfiguration, {buildListener("route_config_0", "cluster_0")},
      {buildListener("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "", {"route_config_0"}, {},
                              {}, Grpc::Status::GrpcStatus::Internal,
                              fmt::format("{} does not match {}", Config::TypeUrl::get().Listener,
                                          Config::TypeUrl::get().RouteConfiguration)));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
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
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));

  // Send duplicate listeners and validate that the update is rejected.
  sendDiscoveryResponse<envoy::api::v2::Listener>(
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
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // Validate that we can process a request.
  makeSingleRequest();

  // Update existing LDS (change stat_prefix).
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0", "rds_crash")},
      {buildListener("listener_0", "route_config_0", "rds_crash")}, {}, "2");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  // Update existing RDS (no changes).
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "2");

  // Validate that we can process a request again
  makeSingleRequest();
}

// Validate that the request with duplicate clusters in the initial request during server init is
// rejected.
TEST_P(AdsIntegrationTest, DuplicateInitialClusters) {
  initialize();

  // Send initial configuration, failing each xDS once (via a type mismatch), validate we can
  // process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(
      Config::TypeUrl::get().Cluster,
      {buildCluster("duplicate_cluster"), buildCluster("duplicate_cluster")},
      {buildCluster("duplicate_cluster"), buildCluster("duplicate_cluster")}, {}, "1");

  test_server_->waitForCounterGe("cluster_manager.cds.update_rejected", 1);
}

// Validate that the request with duplicate clusters in the subsequent requests (warming clusters)
// is rejected.
TEST_P(AdsIntegrationTest, DuplicateWarmingClusters) {
  initialize();

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Send duplicate warming clusters and validate that the update is rejected.
  sendDiscoveryResponse<envoy::api::v2::Cluster>(
      Config::TypeUrl::get().Cluster,
      {buildCluster("duplicate_cluster"), buildCluster("duplicate_cluster")},
      {buildCluster("duplicate_cluster"), buildCluster("duplicate_cluster")}, {}, "2");
  test_server_->waitForCounterGe("cluster_manager.cds.update_rejected", 1);
}

// Verify CDS is paused during cluster warming.
TEST_P(AdsIntegrationTest, CdsPausedDuringWarming) {
  initialize();

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));

  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  EXPECT_FALSE(
      test_server_->server().clusterManager().adsMux().paused(Config::TypeUrl::get().Cluster));
  // Send the first warming cluster.
  sendDiscoveryResponse<envoy::api::v2::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("warming_cluster_1")},
      {buildCluster("warming_cluster_1")}, {"cluster_0"}, "2");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  EXPECT_TRUE(
      test_server_->server().clusterManager().adsMux().paused(Config::TypeUrl::get().Cluster));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"warming_cluster_1"}, {"warming_cluster_1"}, {"cluster_0"}));

  // Send the second warming cluster.
  sendDiscoveryResponse<envoy::api::v2::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("warming_cluster_2")},
      {buildCluster("warming_cluster_2")}, {"warming_cluster_1"}, "3");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 2);
  // We would've got a Cluster discovery request with version 2 here, had the CDS not been paused.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"warming_cluster_2", "warming_cluster_1"},
                                      {"warming_cluster_2"}, {}));

  EXPECT_TRUE(
      test_server_->server().clusterManager().adsMux().paused(Config::TypeUrl::get().Cluster));
  // Finish warming the clusters.
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment("warming_cluster_1"),
       buildClusterLoadAssignment("warming_cluster_2")},
      {buildClusterLoadAssignment("warming_cluster_1"),
       buildClusterLoadAssignment("warming_cluster_2")},
      {"cluster_0"}, "2");

  // Validate that clusters are warmed.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  EXPECT_FALSE(
      test_server_->server().clusterManager().adsMux().paused(Config::TypeUrl::get().Cluster));

  // CDS is resumed and EDS response was acknowledged.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "3", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "2",
                                      {"warming_cluster_2", "warming_cluster_1"}, {}, {}));
}

// Verify cluster warming is finished only on named EDS response.
TEST_P(AdsIntegrationTest, ClusterWarmingOnNamedResponse) {
  initialize();

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));

  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Send the first warming cluster.
  sendDiscoveryResponse<envoy::api::v2::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("warming_cluster_1")},
      {buildCluster("warming_cluster_1")}, {"cluster_0"}, "2");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"warming_cluster_1"}, {"warming_cluster_1"}, {"cluster_0"}));

  // Send the second warming cluster.
  sendDiscoveryResponse<envoy::api::v2::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("warming_cluster_2")},
      {buildCluster("warming_cluster_2")}, {"warming_cluster_1"}, "3");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 2);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"warming_cluster_2", "warming_cluster_1"},
                                      {"warming_cluster_2"}, {}));

  // Finish warming the first cluster.
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment("warming_cluster_1")},
      {buildClusterLoadAssignment("warming_cluster_1")}, {"cluster_0"}, "2");

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
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment("warming_cluster_2")},
      {buildClusterLoadAssignment("warming_cluster_2")}, {"warming_cluster_1"}, "3");

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
}

// Regression test for the use-after-free crash when processing RDS update (#3953).
TEST_P(AdsIntegrationTest, RdsAfterLdsWithRdsChange) {
  initialize();

  // Send initial configuration.
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // Validate that we can process a request.
  makeSingleRequest();

  // Update existing LDS (change stat_prefix).
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_1")},
                                                 {buildCluster("cluster_1")}, {"cluster_0"}, "2");
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_1")},
      {buildClusterLoadAssignment("cluster_1")}, {"cluster_0"}, "2");
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0", "rds_crash")},
      {buildListener("listener_0", "route_config_0", "rds_crash")}, {}, "2");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  // Update existing RDS (migrate traffic to cluster_1).
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
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
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");

  // Initial request for load assignment for cluster_0, respond with version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  // Request for updates to cluster_0 version 1, no response
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  // Initial request for any listener, respond with listener_0 version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");

  // Request for updates to load assignment version 1, no response
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}));

  // Initial request for route_config_0 (referenced by listener_0), respond with version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
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
  sendDiscoveryResponse<envoy::api::v2::Listener>(
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
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_1", "omg")},
      {buildListener("listener_0", "route_config_1", "omg")}, {}, "3");

  // Respond to prior request for route_config_1. Under the hood, this invokes
  // RdsRouteConfigSubscription::runInitializeCallbackIfAny, which references the defunct
  // ListenerImpl instance. We should not crash in this event!
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_1", "cluster_0")},
      {buildRouteConfig("route_config_1", "cluster_0")}, {"route_config_0"}, "1");

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
}

class AdsFailIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                               public HttpIntegrationTest {
public:
  AdsFailIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), AdsIntegrationConfig()) {
    create_xds_upstream_ = true;
    use_lds_ = false;
  }

  void TearDown() override {
    cleanUpXdsConnection();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
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

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, AdsFailIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Validate that we don't crash on failed ADS stream.
TEST_P(AdsFailIntegrationTest, ConnectDisconnect) {
  initialize();
  createXdsConnection();
  ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
  xds_stream_->startGrpcStream();
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);
}

class AdsConfigIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                 public HttpIntegrationTest {
public:
  AdsConfigIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), AdsIntegrationConfig()) {
    create_xds_upstream_ = true;
    use_lds_ = false;
  }

  void TearDown() override {
    cleanUpXdsConnection();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* grpc_service =
          bootstrap.mutable_dynamic_resources()->mutable_ads_config()->add_grpc_services();
      setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
      auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ads_cluster->set_name("ads_cluster");

      // Add EDS static Cluster that uses ADS as config Source.
      auto* ads_eds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_eds_cluster->set_name("ads_eds_cluster");
      ads_eds_cluster->set_type(envoy::api::v2::Cluster::EDS);
      auto* eds_cluster_config = ads_eds_cluster->mutable_eds_cluster_config();
      auto* eds_config = eds_cluster_config->mutable_eds_config();
      eds_config->mutable_ads();
    });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, AdsConfigIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

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
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
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
                                        {"eds_cluster2", "eds_cluster"}, {}));
    sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment,
        {buildClusterLoadAssignment("eds_cluster"), buildClusterLoadAssignment("eds_cluster2")},
        {buildClusterLoadAssignment("eds_cluster"), buildClusterLoadAssignment("eds_cluster2")}, {},
        "1");

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                        {"route_config2", "route_config"},
                                        {"route_config2", "route_config"}, {}));
    sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
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
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");

  // Initial request for load assignment for cluster_0, respond with version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");
  // Request for updates to cluster_0 version 1, no response
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  // Initial request for any listener, respond with listener_0 version 1
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Listener>(
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
  EXPECT_EQ(getListenersConfigDump().dynamic_warming_listeners().size(), 1);

  // Remove listener.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  sendDiscoveryResponse<envoy::api::v2::Listener>(Config::TypeUrl::get().Listener, {}, {}, {}, "1");
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 0);
}

} // namespace Envoy
