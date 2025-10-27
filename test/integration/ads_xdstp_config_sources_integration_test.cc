#include "test/integration/ads_xdstp_config_sources_integration.h"

using testing::AssertionResult;

namespace Envoy {

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeltaWildcard, AdsXdsTpConfigsIntegrationTest,
                         ADS_INTEGRATION_PARAMS);

// Validate that clusters that are fetched using ADS and use EDS from a
// different authority works.
TEST_P(AdsXdsTpConfigsIntegrationTest, CdsPointsToAuthorityEds) {
  setupClustersFromOldAds();
  setupListenersFromOldAds();
  initialize();

  // Wait for ADS clusters request and send a cluster that points to load
  // assignment in authority1.com.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, {}, {}, true));
  auto cluster1 = ConfigHelper::buildCluster("cluster_1");
  cluster1.mutable_eds_cluster_config()->set_service_name(
      "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1");
  cluster1.mutable_eds_cluster_config()->clear_eds_config();
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1}, {cluster1}, {}, "1");

  // Authority1 receives an EDS request, and sends a response.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "",
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {}, true, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TestTypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment(
          "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
          "cluster1")},
      {buildClusterLoadAssignment(
          "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
          "cluster1")},
      {}, "1", {}, authority1_xds_stream_.get());

  // Old ADS receives a CDS ACK.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {}, {}, {}));

  // Authority1 receives an EDS ACK.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "1",
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));

  // Send the Listener and route config using the old ADS.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TestTypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TestTypeUrl::get().RouteConfiguration,
      {ConfigHelper::buildRouteConfig("route_config_0", "cluster_1")},
      {ConfigHelper::buildRouteConfig("route_config_0", "cluster_1")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();
}

// Validate that updating the EDS contents by a different authority works.
TEST_P(AdsXdsTpConfigsIntegrationTest, UpdateAuthorityEds) {
  setupClustersFromOldAds();
  setupListenersFromOldAds();
  initialize();

  // Wait for ADS clusters request and send a cluster that points to load
  // assignment in authority1.com.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, {}, {}, true));
  auto cluster1 = ConfigHelper::buildCluster("cluster_1");
  cluster1.mutable_eds_cluster_config()->set_service_name(
      "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1");
  cluster1.mutable_eds_cluster_config()->clear_eds_config();
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1}, {cluster1}, {}, "1");

  // Authority1 receives an EDS request, and sends a response.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "",
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {}, true, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  envoy::config::endpoint::v3::ClusterLoadAssignment cla = buildClusterLoadAssignment(
      "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1");
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TestTypeUrl::get().ClusterLoadAssignment, {cla}, {cla}, {}, "1", {},
      authority1_xds_stream_.get());

  // Old ADS receives a CDS ACK.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {}, {}, {}));

  // Authority1 receives an EDS ACK.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "1",
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  test_server_->waitForCounterGe("cluster.cluster_1.update_success", 1);

  // Send the Listener and route config using the old ADS.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TestTypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TestTypeUrl::get().RouteConfiguration,
      {ConfigHelper::buildRouteConfig("route_config_0", "cluster_1")},
      {ConfigHelper::buildRouteConfig("route_config_0", "cluster_1")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // Update the EDS config.
  cla.mutable_endpoints(0)->mutable_load_balancing_weight()->set_value(50);
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TestTypeUrl::get().ClusterLoadAssignment, {cla}, {cla}, {}, "2", {},
      authority1_xds_stream_.get());

  // Expect an EDS ACK.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "2",
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));

  // Ensure that the EDS update was successful.
  test_server_->waitForCounterGe("cluster.cluster_1.update_success", 2);
  makeSingleRequest();
}

// Validate that when ADS returns a cluster update with a resource from another
// config-source, the new resource is subscribed to, and the old one is
// unsubscribed.
TEST_P(AdsXdsTpConfigsIntegrationTest, UpdateAuthorityToFetchEds) {
  setupClustersFromOldAds();
  setupListenersFromOldAds();
  initialize();

  // Wait for ADS clusters request and send a cluster that points to load
  // assignment in authority1.com.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, {}, {}, true));
  auto cluster1 = ConfigHelper::buildCluster("cluster_1");
  cluster1.mutable_eds_cluster_config()->set_service_name(
      "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1");
  cluster1.mutable_eds_cluster_config()->clear_eds_config();
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1}, {cluster1}, {}, "1");

  // Authority1 receives an EDS request, and sends a response.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "",
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {}, true, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TestTypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment(
          "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
          "cluster1")},
      {buildClusterLoadAssignment(
          "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
          "cluster1")},
      {}, "1", {}, authority1_xds_stream_.get());

  // Old ADS receives a CDS ACK.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {}, {}, {}));

  // Authority1 receives an EDS ACK.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "1",
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  test_server_->waitForCounterGe("cluster.cluster_1.update_success", 1);

  // Send the Listener and route config using the old ADS.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TestTypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "",
                                      {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TestTypeUrl::get().RouteConfiguration,
      {ConfigHelper::buildRouteConfig("route_config_0", "cluster_1")},
      {ConfigHelper::buildRouteConfig("route_config_0", "cluster_1")}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Update the cluster's load-assignment to a different resource authority.
  cluster1.mutable_eds_cluster_config()->set_service_name(
      "xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
      "cluster1");

  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {cluster1}, {cluster1}, {}, "2");

  // Default-authority receives an EDS request, and sends a response.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "",
      {"xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
       "cluster1"},
      {"xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
       "cluster1"},
      {}, true, Grpc::Status::WellKnownGrpcStatus::Ok, "", default_authority_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TestTypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment(
          "xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
          "cluster1")},
      {buildClusterLoadAssignment(
          "xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
          "cluster1")},
      {}, "2", {}, default_authority_xds_stream_.get());

  // Old ADS receives a CDS ACK.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "2", {}, {}, {}));

  // Default-authority receives an EDS ACK.
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TestTypeUrl::get().ClusterLoadAssignment, "2",
                              {"xdstp://default_authority.com/"
                               "envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
                              {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
                              default_authority_xds_stream_.get()));
  test_server_->waitForCounterGe("cluster.cluster_1.update_success", 2);

  // Authority1 subscription is removed.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "", {}, {},
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      false, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));

  // Try sending a message to the backend.
  makeSingleRequest();
}
} // namespace Envoy
