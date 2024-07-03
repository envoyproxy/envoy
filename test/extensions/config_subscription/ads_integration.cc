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
#include "test/integration/ads_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/printers.h"
#include "test/test_common/resources.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class XdsTpAdsIntegrationTest : public AdsIntegrationTest {
public:
  void initialize() override {
    const bool is_sotw = isSotw();
    config_helper_.addConfigModifier([is_sotw](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.add_node_context_params("id");
      bootstrap.add_node_context_params("cluster");
      bootstrap.mutable_dynamic_resources()->set_cds_resources_locator(
          "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/*");
      auto* cds_config = bootstrap.mutable_dynamic_resources()->mutable_cds_config();
      cds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      cds_config->mutable_api_config_source()->set_api_type(
          is_sotw ? envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC
                  : envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC);
      cds_config->mutable_api_config_source()->set_transport_api_version(
          envoy::config::core::v3::V3);
      bootstrap.mutable_dynamic_resources()->set_lds_resources_locator(
          "xdstp://test/envoy.config.listener.v3.Listener/foo-listener/*");
      auto* lds_config = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
      lds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      lds_config->mutable_api_config_source()->set_api_type(
          is_sotw ? envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC
                  : envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC);
      lds_config->mutable_api_config_source()->set_transport_api_version(
          envoy::config::core::v3::V3);
      auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
      ads_config->set_set_node_on_first_message_only(true);
    });
    AdsIntegrationTest::initialize();
  }

  bool isSotw() const {
    return sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
           sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw;
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeltaWildcard, XdsTpAdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     // There should be no variation across clients.
                     testing::Values(Grpc::ClientType::EnvoyGrpc),
                     testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::UnifiedSotw,
                                     Grpc::SotwOrDelta::Delta, Grpc::SotwOrDelta::UnifiedDelta)));

TEST_P(XdsTpAdsIntegrationTest, Basic) {
  initialize();
  // Basic CDS/EDS xDS initialization (CDS via xdstp:// glob collection).
  const std::string cluster_wildcard = "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                       "*?xds.node.cluster=cluster_name&xds.node.id=node_name";
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {cluster_wildcard},
                                      {cluster_wildcard}, {}, /*expect_node=*/true));
  const std::string cluster_name = "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                   "baz?xds.node.cluster=cluster_name&xds.node.id=node_name";
  auto cluster_resource = buildCluster(cluster_name);
  const std::string endpoints_name =
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz";
  cluster_resource.mutable_eds_cluster_config()->set_service_name(endpoints_name);
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {cluster_resource}, {cluster_resource}, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {endpoints_name}, {endpoints_name}, {}));

  const auto cluster_load_assignments = {buildClusterLoadAssignment(endpoints_name)};
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, cluster_load_assignments,
      cluster_load_assignments, {}, "1");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  // LDS/RDS xDS initialization (LDS via xdstp:// glob collection)
  const std::string listener_wildcard =
      "xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
      "*?xds.node.cluster=cluster_name&xds.node.id=node_name";
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {listener_wildcard},
                                      {listener_wildcard}, {}));
  const std::string route_name_0 =
      "xdstp://test/envoy.config.route.v3.RouteConfiguration/route_config_0";
  const std::string route_name_1 =
      "xdstp://test/envoy.config.route.v3.RouteConfiguration/route_config_1";
  const auto listeners = {
      buildListener("xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                    "bar?xds.node.cluster=cluster_name&xds.node.id=node_name",
                    route_name_0),
      // Ignore resource in impostor namespace.
      buildListener("xdstp://test/envoy.config.listener.v3.Listener/impostor/"
                    "bar?xds.node.cluster=cluster_name&xds.node.id=node_name",
                    route_name_0),
      // Ignore non-matching context params.
      buildListener("xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                    "baz?xds.node.cluster=cluster_name&xds.node.id=other_name",
                    route_name_0),
  };
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(Config::TypeUrl::get().Listener,
                                                               listeners, listeners, {}, "1");
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1", {}, {}, {}));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "", {route_name_0},
                                      {route_name_0}, {}));
  const auto route_config = buildRouteConfig(route_name_0, cluster_name);
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {route_config}, {route_config}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1", {}, {}, {}));

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Add a second listener in the foo namespace.
  const auto baz_listener =
      buildListener("xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                    "baz?xds.node.cluster=cluster_name&xds.node.id=node_name",
                    route_name_1);
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {baz_listener}, {baz_listener}, {}, "2");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {route_name_1}, {route_name_1}, {}));
  const auto second_route_config = buildRouteConfig(route_name_1, cluster_name);
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {second_route_config}, {second_route_config}, {},
      "2");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "2", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2", {}, {}, {}));

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 2);
  makeSingleRequest();

  // Updates only apply to the Delta protocol, not SotW.
  const std::string bar_listener = "xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                                   "bar?xds.node.cluster=cluster_name&xds.node.id=node_name";

  if (isSotw()) {
    // In SotW, removal consists of sending the other listeners, except for the one to be removed.
    sendDiscoveryResponse<envoy::config::listener::v3::Listener>(Config::TypeUrl::get().Listener,
                                                                 {baz_listener}, {}, {}, "3");
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "3", {}, {}, {}));
    test_server_->waitForCounterEq("listener_manager.listener_removed", 1);
    makeSingleRequest();
  } else {
    // Update bar listener in the foo namespace.
    sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
        Config::TypeUrl::get().Listener, {}, {buildListener(bar_listener, route_name_1)}, {}, "3");
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "3", {}, {}, {}));
    test_server_->waitForCounterEq("listener_manager.listener_in_place_updated", 1);
    makeSingleRequest();

    // Remove bar listener from the foo namespace.
    sendDiscoveryResponse<envoy::config::listener::v3::Listener>(Config::TypeUrl::get().Listener,
                                                                 {}, {}, {bar_listener}, "3");
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "4", {}, {}, {}));
    test_server_->waitForCounterEq("listener_manager.listener_removed", 1);
    makeSingleRequest();
  }
}

// Basic CDS/EDS/LEDS update that warms and makes active a single cluster.
TEST_P(XdsTpAdsIntegrationTest, BasicWithLeds) {
  if (isSotw()) {
    // LEDS only works with the Delta protocol.
    return;
  }

  initialize();

  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
  const auto eds_type_url =
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>();
  const auto leds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>();

  // Receive CDS request, and send a cluster with EDS.
  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {},
                                      {"xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                       "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                                      {}, true));
  const std::string cluster_name = "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                   "baz?xds.node.cluster=cluster_name&xds.node.id=node_name";
  auto cluster_resource = buildCluster(cluster_name);
  const std::string endpoints_name =
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz";
  cluster_resource.mutable_eds_cluster_config()->set_service_name(endpoints_name);
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cds_type_url, {}, {cluster_resource},
                                                             {}, "1");

  // Receive EDS request, and send ClusterLoadAssignment with one locality, that uses LEDS.
  const auto leds_resource_prefix =
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/";
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {}, {endpoints_name}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {},
      {buildClusterLoadAssignmentWithLeds(endpoints_name, absl::StrCat(leds_resource_prefix, "*"))},
      {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  // Receive LEDS request, and send 2 endpoints.
  EXPECT_TRUE(compareDiscoveryRequest(
      leds_type_url, "", {},
      {absl::StrCat(leds_resource_prefix, "*?xds.node.cluster=cluster_name&xds.node.id=node_name")},
      {}));
  const auto endpoint1_name = absl::StrCat(leds_resource_prefix, "endpoint_0",
                                           "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  const auto endpoint2_name = absl::StrCat(leds_resource_prefix, "endpoint_1",
                                           "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  sendExplicitResourcesDeltaDiscoveryResponse(
      Config::TypeUrl::get().LbEndpoint,
      {buildLbEndpointResource(endpoint1_name, "2"), buildLbEndpointResource(endpoint2_name, "2")},
      {});

  // Receive the EDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "1", {}, {}, {}));

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);

  // LDS/RDS xDS initialization (LDS via xdstp:// glob collection)
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {},
                              {"xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                               "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                              {}));

  // Receive the LEDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(leds_type_url, "2", {}, {}, {}));
}

// CDS/EDS/LEDS update that warms and makes active a single cluster. While
// waiting for LEDS a new EDS update arrives.
TEST_P(XdsTpAdsIntegrationTest, LedsClusterWarmingUpdatingEds) {
  if (isSotw()) {
    // LEDS only works with the Delta protocol.
    return;
  }

  initialize();

  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
  const auto eds_type_url =
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>();
  const auto leds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>();

  // Receive CDS request, and send a cluster with EDS.
  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {},
                                      {"xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                       "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                                      {}, true));
  const std::string cluster_name = "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                   "baz?xds.node.cluster=cluster_name&xds.node.id=node_name";
  auto cluster_resource = buildCluster(cluster_name);
  const std::string endpoints_name =
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz";
  cluster_resource.mutable_eds_cluster_config()->set_service_name(endpoints_name);
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cds_type_url, {}, {cluster_resource},
                                                             {}, "1");

  // Receive EDS request, and send ClusterLoadAssignment with one locality, that uses LEDS.
  const auto leds_resource_prefix_foo =
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/";
  const auto leds_resource_prefix_bar =
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/bar-endpoints/";
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {}, {endpoints_name}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {},
      {buildClusterLoadAssignmentWithLeds(endpoints_name,
                                          absl::StrCat(leds_resource_prefix_foo, "*"))},
      {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  // Receive LEDS request, and send an updated EDS response.
  EXPECT_TRUE(compareDiscoveryRequest(
      leds_type_url, "", {},
      {absl::StrCat(leds_resource_prefix_foo,
                    "*?xds.node.cluster=cluster_name&xds.node.id=node_name")},
      {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {},
      {buildClusterLoadAssignmentWithLeds(endpoints_name,
                                          absl::StrCat(leds_resource_prefix_bar, "*"))},
      {}, "2");
  // Receive the EDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "1", {}, {}, {}));

  // Send the old LEDS response, and ensure it is rejected.
  const auto endpoint1_name_foo =
      absl::StrCat(leds_resource_prefix_foo, "endpoint_0",
                   "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  const auto endpoint2_name_foo =
      absl::StrCat(leds_resource_prefix_foo, "endpoint_1",
                   "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  sendExplicitResourcesDeltaDiscoveryResponse(Config::TypeUrl::get().LbEndpoint,
                                              {buildLbEndpointResource(endpoint1_name_foo, "2"),
                                               buildLbEndpointResource(endpoint2_name_foo, "2")},
                                              {});

  // Receive the new LEDS request and EDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(
      leds_type_url, "", {},
      {absl::StrCat(leds_resource_prefix_bar,
                    "*?xds.node.cluster=cluster_name&xds.node.id=node_name")},
      {absl::StrCat(leds_resource_prefix_foo,
                    "*?xds.node.cluster=cluster_name&xds.node.id=node_name")}));
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "2", {}, {}, {}));

  // Send the new LEDS response
  const auto endpoint1_name_bar =
      absl::StrCat(leds_resource_prefix_bar, "endpoint_0",
                   "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  const auto endpoint2_name_bar =
      absl::StrCat(leds_resource_prefix_bar, "endpoint_1",
                   "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  sendExplicitResourcesDeltaDiscoveryResponse(Config::TypeUrl::get().LbEndpoint,
                                              {buildLbEndpointResource(endpoint1_name_bar, "3"),
                                               buildLbEndpointResource(endpoint2_name_bar, "3")},
                                              {});

  // The cluster should be warmed up.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);

  // Receive the LEDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(leds_type_url, "3", {}, {}, {}));

  // LDS/RDS xDS initialization (LDS via xdstp:// glob collection)
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {},
                              {"xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                               "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                              {}));

  // Receive the LEDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(leds_type_url, "2", {}, {}, {}));
}

// CDS/EDS/LEDS update that warms and makes active a single cluster. While
// waiting for LEDS a new CDS update arrives.
TEST_P(XdsTpAdsIntegrationTest, LedsClusterWarmingUpdatingCds) {
  if (isSotw()) {
    // LEDS only works with the Delta protocol.
    return;
  }

  initialize();

  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
  const auto eds_type_url =
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>();
  const auto leds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>();

  // Receive CDS request, and send a cluster with EDS.
  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {},
                                      {"xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                       "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                                      {}, true));
  const std::string cluster1_name = "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                    "cluster1?xds.node.cluster=cluster_name&xds.node.id=node_name";
  auto cluster1_resource = buildCluster(cluster1_name);
  const std::string endpoints1_name =
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/cluster1";
  cluster1_resource.mutable_eds_cluster_config()->set_service_name(endpoints1_name);
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cds_type_url, {}, {cluster1_resource},
                                                             {}, "1");

  // Receive EDS request, and send ClusterLoadAssignment with one locality, that uses LEDS.
  const auto leds_resource_prefix1 =
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints1/";
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {}, {endpoints1_name}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {},
      {buildClusterLoadAssignmentWithLeds(endpoints1_name,
                                          absl::StrCat(leds_resource_prefix1, "*"))},
      {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  // Receive LEDS request, and send an updated CDS response (removing previous
  // cluster and adding a new one).
  EXPECT_TRUE(compareDiscoveryRequest(
      leds_type_url, "", {},
      {absl::StrCat(leds_resource_prefix1,
                    "*?xds.node.cluster=cluster_name&xds.node.id=node_name")},
      {}));
  const std::string cluster2_name = "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                    "cluster2?xds.node.cluster=cluster_name&xds.node.id=node_name";
  auto cluster2_resource = buildCluster(cluster2_name);
  const std::string endpoints2_name =
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/cluster2";
  cluster2_resource.mutable_eds_cluster_config()->set_service_name(endpoints2_name);
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cds_type_url, {}, {cluster2_resource},
                                                             {cluster1_name}, "2");

  // Receive the EDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "1", {}, {}, {}));

  // Send the old LEDS response.
  const auto endpoint1_name_cluster1 = absl::StrCat(
      leds_resource_prefix1, "endpoint_0", "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  const auto endpoint2_name_cluster1 = absl::StrCat(
      leds_resource_prefix1, "endpoint_1", "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  sendExplicitResourcesDeltaDiscoveryResponse(
      Config::TypeUrl::get().LbEndpoint,
      {buildLbEndpointResource(endpoint1_name_cluster1, "2"),
       buildLbEndpointResource(endpoint2_name_cluster1, "2")},
      {});

  // Receive EDS request, and send ClusterLoadAssignment with one locality, that uses LEDS.
  const auto leds_resource_prefix2 =
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints2/";
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {}, {endpoints2_name}, {endpoints1_name}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {},
      {buildClusterLoadAssignmentWithLeds(endpoints2_name,
                                          absl::StrCat(leds_resource_prefix2, "*"))},
      {}, "2");

  // The server should remove interest in the old LEDS.
  EXPECT_TRUE(compareDiscoveryRequest(
      leds_type_url, "", {}, {},
      {absl::StrCat(leds_resource_prefix1,
                    "*?xds.node.cluster=cluster_name&xds.node.id=node_name")}));

  // Receive CDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "2", {}, {}, {}));

  // Receive the EDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(leds_type_url, "2", {}, {}, {}));

  // Receive the new LEDS request and EDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(
      leds_type_url, "", {},
      {absl::StrCat(leds_resource_prefix2,
                    "*?xds.node.cluster=cluster_name&xds.node.id=node_name")},
      {}));
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "2", {}, {}, {}));

  // Send 2 endpoints using LEDS.
  const auto endpoint1_name_cluster2 = absl::StrCat(
      leds_resource_prefix2, "endpoint_0", "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  const auto endpoint2_name_cluster2 = absl::StrCat(
      leds_resource_prefix2, "endpoint_1", "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  sendExplicitResourcesDeltaDiscoveryResponse(
      Config::TypeUrl::get().LbEndpoint,
      {buildLbEndpointResource(endpoint1_name_cluster2, "2"),
       buildLbEndpointResource(endpoint2_name_cluster2, "2")},
      {});

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);

  // LDS/RDS xDS initialization (LDS via xdstp:// glob collection)
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {},
                              {"xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                               "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                              {}));

  // Receive the LEDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(leds_type_url, "2", {}, {}, {}));
}

// Timeout on LEDS update activates the cluster.
TEST_P(XdsTpAdsIntegrationTest, LedsTimeout) {
  if (isSotw()) {
    // LEDS only works with the Delta protocol.
    return;
  }

  initialize();

  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
  const auto eds_type_url =
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>();
  const auto leds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>();

  // Receive CDS request, and send a cluster with EDS.
  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {},
                                      {"xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                       "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                                      {}, true));
  const std::string cluster_name = "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                   "baz?xds.node.cluster=cluster_name&xds.node.id=node_name";
  auto cluster_resource = buildCluster(cluster_name);
  const std::string endpoints_name =
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz";
  cluster_resource.mutable_eds_cluster_config()->set_service_name(endpoints_name);
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cds_type_url, {}, {cluster_resource},
                                                             {}, "1");

  // Receive EDS request, and send ClusterLoadAssignment with one locality, that uses LEDS.
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {}, {endpoints_name}, {}));
  const auto leds_resource_prefix =
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/";

  auto cla_with_leds =
      buildClusterLoadAssignmentWithLeds(endpoints_name, absl::StrCat(leds_resource_prefix, "*"));
  // Set a short timeout for the initial fetch.
  cla_with_leds.mutable_endpoints(0)
      ->mutable_leds_cluster_locality_config()
      ->mutable_leds_config()
      ->mutable_initial_fetch_timeout()
      ->set_nanos(100 * 1000 * 1000);
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {}, {cla_with_leds}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));
  // Receive LEDS request, and wait for the initial fetch timeout.
  EXPECT_TRUE(compareDiscoveryRequest(
      leds_type_url, "", {},
      {absl::StrCat(leds_resource_prefix, "*?xds.node.cluster=cluster_name&xds.node.id=node_name")},
      {}));

  // The cluster should be warming. Wait until initial fetch timeout.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  test_server_->waitForCounterEq(
      "cluster.xdstp_test/envoy.config.cluster.v3.Cluster/foo-cluster/"
      "baz?xds.node.cluster=cluster_name&xds.node.id=node_name.leds.init_fetch_timeout",
      1);

  // After timeout the cluster should be active, not warming.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);

  // Receive the EDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "1", {}, {}, {}));

  // LDS/RDS xDS initialization (LDS via xdstp:// glob collection)
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {},
                              {"xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                               "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                              {}));
}

// Modifying a cluster to alternate use of EDS with and without LEDS.
TEST_P(XdsTpAdsIntegrationTest, EdsAlternatingLedsUsage) {
  if (isSotw()) {
    // LEDS only works with the Delta protocol.
    return;
  }

  initialize();

  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
  const auto eds_type_url =
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>();
  const auto leds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>();

  // Receive CDS request, and send a cluster with EDS.
  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {},
                                      {"xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                       "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                                      {}, true));
  const std::string cluster_name = "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                   "baz?xds.node.cluster=cluster_name&xds.node.id=node_name";
  auto cluster_resource = buildCluster(cluster_name);
  const std::string endpoints_name =
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz";
  cluster_resource.mutable_eds_cluster_config()->set_service_name(endpoints_name);
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cds_type_url, {}, {cluster_resource},
                                                             {}, "1");

  // Receive EDS request, and send ClusterLoadAssignment with one locality,
  // that doesn't use LEDS.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "", {},
                                      {endpoints_name}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {},
      {buildClusterLoadAssignment(endpoints_name)}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "1", {}, {}, {}));

  // LDS/RDS xDS initialization (LDS via xdstp:// glob collection)
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {},
                              {"xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                               "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                              {}));
  const std::string route_name_0 =
      "xdstp://test/envoy.config.route.v3.RouteConfiguration/route_config_0";
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {},
      {buildListener("xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                     "bar?xds.node.cluster=cluster_name&xds.node.id=node_name",
                     route_name_0)},
      {}, "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "", {},
                                      {route_name_0}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {}, {buildRouteConfig(route_name_0, cluster_name)},
      {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1", {}, {}, {}));

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Send a new EDS update that uses LEDS.
  const auto leds_resource_prefix =
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/";
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {},
      {buildClusterLoadAssignmentWithLeds(endpoints_name, absl::StrCat(leds_resource_prefix, "*"))},
      {}, "2");

  // Receive LEDS request.
  EXPECT_TRUE(compareDiscoveryRequest(
      leds_type_url, "", {},
      {absl::StrCat(leds_resource_prefix, "*?xds.node.cluster=cluster_name&xds.node.id=node_name")},
      {}));

  // Make sure that traffic can still be sent to the endpoint (still using the
  // EDS without LEDS).
  makeSingleRequest();

  // Receive the EDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "2", {}, {}, {}));

  // Send LEDS response with 2 endpoints.
  const auto endpoint1_name = absl::StrCat(leds_resource_prefix, "endpoint_0",
                                           "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  const auto endpoint2_name = absl::StrCat(leds_resource_prefix, "endpoint_1",
                                           "?xds.node.cluster=cluster_name&xds.node.id=node_name");
  sendExplicitResourcesDeltaDiscoveryResponse(
      Config::TypeUrl::get().LbEndpoint,
      {buildLbEndpointResource(endpoint1_name, "1"), buildLbEndpointResource(endpoint2_name, "1")},
      {});

  // Receive the LEDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(leds_type_url, "1", {}, {}, {}));

  // Make sure that traffic can still be sent to the endpoint (now using the
  // EDS with LEDS).
  makeSingleRequest();

  // Send a new EDS update that doesn't use LEDS.
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {},
      {buildClusterLoadAssignment(endpoints_name)}, {}, "3");

  // The server should remove interest in the old LEDS.
  EXPECT_TRUE(compareDiscoveryRequest(
      leds_type_url, "", {}, {},
      {absl::StrCat(leds_resource_prefix,
                    "*?xds.node.cluster=cluster_name&xds.node.id=node_name")}));

  // Receive the EDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "3", {}, {}, {}));

  // Remove the LEDS endpoints.
  sendExplicitResourcesDeltaDiscoveryResponse(Config::TypeUrl::get().LbEndpoint, {},
                                              {endpoint1_name, endpoint2_name});

  // Receive the LEDS ack.
  EXPECT_TRUE(compareDiscoveryRequest(leds_type_url, "3", {}, {}, {}));

  // Make sure that traffic can still be sent to the endpoint (now using the
  // EDS without LEDS).
  makeSingleRequest();
}

} // namespace
} // namespace Envoy
