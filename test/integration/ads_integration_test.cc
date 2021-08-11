#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/grpc/status.h"

#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/config/version_converter.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/version/version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
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

// Basic CDS/EDS update that warms and makes active a single cluster.
TEST_P(AdsIntegrationTest, BasicClusterInitialWarming) {
  initialize();
  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V3);
  const auto eds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      envoy::config::core::v3::ApiVersion::V3);

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("cluster_0")}, {buildCluster("cluster_0")}, {}, "1", false);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1", false);

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
}

// Update the only warming cluster. Verify that the new cluster is still warming and the cluster
// manager as a whole is not initialized.
TEST_P(AdsIntegrationTest, ClusterInitializationUpdateTheOnlyWarmingCluster) {
  initialize();
  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V3);
  const auto eds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      envoy::config::core::v3::ApiVersion::V3);

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("cluster_0")}, {buildCluster("cluster_0")}, {}, "1", false);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  // Update lb policy to MAGLEV so that cluster update is not skipped due to the same hash.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("cluster_0", "MAGLEV")}, {buildCluster("cluster_0", "MAGLEV")},
      {}, "2", false);
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1", false);

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
}

// Primary cluster is warming during cluster initialization. Update the cluster with immediate ready
// config and verify that all the clusters are initialized.
TEST_P(AdsIntegrationTest, TestPrimaryClusterWarmClusterInitialization) {
  initialize();
  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V3);
  auto loopback = Network::Test::getLoopbackAddressString(ipVersion());
  addFakeUpstream(Http::CodecType::HTTP2);
  auto port = fake_upstreams_.back()->localAddress()->ip()->port();

  // This cluster will be blocked since endpoint name cannot be resolved.
  auto warming_cluster = ConfigHelper::buildStaticCluster("fake_cluster", port, loopback);
  // Below endpoint accepts request but never return. The health check hangs 1 hour which covers the
  // test running.
  auto blocking_health_check = TestUtility::parseYaml<envoy::config::core::v3::HealthCheck>(R"EOF(
      timeout: 3600s
      interval: 3600s
      unhealthy_threshold: 2
      healthy_threshold: 2
      tcp_health_check:
        send:
          text: '01'
        receive:
          - text: '02'
          )EOF");
  *warming_cluster.add_health_checks() = blocking_health_check;

  // Active cluster has the same name with warming cluster but has no blocking health check.
  auto active_cluster = ConfigHelper::buildStaticCluster("fake_cluster", port, loopback);

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cds_type_url, {warming_cluster},
                                                             {warming_cluster}, {}, "1", false);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_.back()->waitForRawConnection(fake_upstream_connection));

  // fake_cluster is in warming.
  test_server_->waitForGaugeGe("cluster_manager.warming_clusters", 1);

  // Now replace the warming cluster by the config which will turn ready immediately.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cds_type_url, {active_cluster},
                                                             {active_cluster}, {}, "2", false);

  // All clusters are ready.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
}

// Two cluster warming, update one of them. Verify that the clusters are eventually initialized.
TEST_P(AdsIntegrationTest, ClusterInitializationUpdateOneOfThe2Warming) {
  initialize();
  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V3);
  const auto eds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      envoy::config::core::v3::ApiVersion::V3);

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url,
      {ConfigHelper::buildStaticCluster("primary_cluster", 8000, "127.0.0.1"),
       buildCluster("cluster_0"), buildCluster("cluster_1")},
      {ConfigHelper::buildStaticCluster("primary_cluster", 8000, "127.0.0.1"),
       buildCluster("cluster_0"), buildCluster("cluster_1")},
      {}, "1", false);

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 2);

  // Update lb policy to MAGLEV so that cluster update is not skipped due to the same hash.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url,
      {ConfigHelper::buildStaticCluster("primary_cluster", 8000, "127.0.0.1"),
       buildCluster("cluster_0", "MAGLEV"), buildCluster("cluster_1")},
      {ConfigHelper::buildStaticCluster("primary_cluster", 8000, "127.0.0.1"),
       buildCluster("cluster_0", "MAGLEV"), buildCluster("cluster_1")},
      {}, "2", false);
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {"cluster_0", "cluster_1"},
                                      {"cluster_0", "cluster_1"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url,
      {buildClusterLoadAssignment("cluster_0"), buildClusterLoadAssignment("cluster_1")},
      {buildClusterLoadAssignment("cluster_0"), buildClusterLoadAssignment("cluster_1")}, {}, "1",
      false);

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 4);
}

// Make sure two clusters sharing same secret are both kept warming before secret
// arrives. Verify that the clusters are eventually initialized.
// This is a regression test of #11120.
TEST_P(AdsIntegrationTest, ClusterSharingSecretWarming) {
  initialize();
  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V3);
  const auto sds_type_url =
      Config::getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>(
          envoy::config::core::v3::ApiVersion::V3);

  envoy::config::core::v3::TransportSocket sds_transport_socket;
  TestUtility::loadFromYaml(R"EOF(
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
        common_tls_context:
          validation_context_sds_secret_config:
            name: validation_context
            sds_config:
              resource_api_version: V3
              ads: {}
  )EOF",
                            sds_transport_socket);
  auto cluster_template = ConfigHelper::buildStaticCluster("cluster", 8000, "127.0.0.1");
  *cluster_template.mutable_transport_socket() = sds_transport_socket;

  auto cluster_0 = cluster_template;
  cluster_0.set_name("cluster_0");
  auto cluster_1 = cluster_template;
  cluster_1.set_name("cluster_1");

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {cluster_0, cluster_1}, {cluster_0, cluster_1}, {}, "1", false);

  EXPECT_TRUE(compareDiscoveryRequest(sds_type_url, "", {"validation_context"},
                                      {"validation_context"}, {}));
  test_server_->waitForGaugeGe("cluster_manager.warming_clusters", 2);

  envoy::extensions::transport_sockets::tls::v3::Secret validation_context;
  TestUtility::loadFromYaml(fmt::format(R"EOF(
    name: validation_context
    validation_context:
      trusted_ca:
        filename: {}
  )EOF",
                                        TestEnvironment::runfilesPath(
                                            "test/config/integration/certs/upstreamcacert.pem")),
                            validation_context);

  sendDiscoveryResponse<envoy::extensions::transport_sockets::tls::v3::Secret>(
      sds_type_url, {validation_context}, {validation_context}, {}, "1");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
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
      Config::TypeUrl::get().Cluster, "", {}, {}, {}, false,
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
                                      {"cluster_0"}, {}, {}, false,
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
      Config::TypeUrl::get().Listener, "", {}, {}, {}, false,
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
                                      {"route_config_0"}, {}, {}, false,
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

// Regression test for https://github.com/envoyproxy/envoy/issues/9682.
TEST_P(AdsIntegrationTest, ResendNodeOnStreamReset) {
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

  // A second CDS request should be sent so that the node is cleared in the cached request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  xds_stream_->finishGrpcStream(Grpc::Status::Internal);
  AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->startGrpcStream();

  // In SotW cluster_0 will be in the resource_names, but in delta-xDS
  // resource_names_subscribe and resource_names_unsubscribe must be empty for
  // a wildcard request (cluster_0 will appear in initial_resource_versions).
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {"cluster_0"}, {}, {}, true));
}

// Verifies that upon stream reconnection:
// - Non-wildcard requests contain all known resources.
// - Wildcard requests contain all known resources in SotW, but no resources in delta-xDS.
// Regression test for https://github.com/envoyproxy/envoy/issues/16063.
TEST_P(AdsIntegrationTest, ResourceNamesOnStreamReset) {
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

  // A second CDS request should be sent so that the node is cleared in the cached request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  xds_stream_->finishGrpcStream(Grpc::Status::Internal);
  AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->startGrpcStream();

  // In SotW cluster_0 will be in the resource_names, but in delta-xDS
  // resource_names_subscribe and resource_names_unsubscribe must be empty for
  // a wildcard request (cluster_0 will appear in initial_resource_versions).
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {"cluster_0"}, {}, {}, true));
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

// Validate that the use of V2 transport version is rejected by default.
TEST_P(AdsIntegrationTest, DEPRECATED_FEATURE_TEST(RejectV2TransportConfigByDefault)) {
  initialize();

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  auto cluster = buildCluster("cluster_0");
  auto* api_config_source =
      cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_api_config_source();
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  api_config_source->set_transport_api_version(envoy::config::core::v3::V2);
  envoy::config::core::v3::GrpcService* grpc_service = api_config_source->add_grpc_services();
  setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {cluster}, {cluster}, {}, "1");
  test_server_->waitForCounterGe("cluster_manager.cds.update_rejected", 1);
  EXPECT_GE(test_server_->gauge("runtime.deprecated_feature_seen_since_process_start")->value(), 1);
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

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "2", {}, {}, {}));
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
      Config::TypeUrl::get().Cluster,
      {buildCluster("warming_cluster_1"), buildCluster("warming_cluster_2")},
      {buildCluster("warming_cluster_1"), buildCluster("warming_cluster_2")}, {}, "3");
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

TEST_P(AdsIntegrationTest, RemoveWarmingCluster) {
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

  // Send the second warming cluster and remove the first cluster.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("warming_cluster_2")},
                                                             {buildCluster("warming_cluster_2")},
                                                             // Delta: remove warming_cluster_1.
                                                             {"warming_cluster_1"}, "3");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  // We would've got a Cluster discovery request with version 2 here, had the CDS not been paused.

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"warming_cluster_2"}, {"warming_cluster_2"},
                                      {"warming_cluster_1"}));

  // Finish warming the clusters. Note that the first warming cluster is not included in the
  // response.
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment("warming_cluster_2")},
      {buildClusterLoadAssignment("warming_cluster_2")}, {"cluster_0"}, "2");

  // Validate that all clusters are warmed.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeEq("cluster_manager.active_clusters", 3);

  // CDS is resumed and EDS response was acknowledged.
  if (sotw_or_delta_ == Grpc::SotwOrDelta::Delta) {
    // Envoy will ACK both Cluster messages. Since they arrived while CDS was paused, they aren't
    // sent until CDS is unpaused. Since version 3 has already arrived by the time the version 2
    // ACK goes out, they're both acknowledging version 3.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "3", {}, {}, {}));
  }
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "3", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "2",
                                      {"warming_cluster_2"}, {}, {}));
}
// Validate that warming listeners are removed when left out of SOTW update.
TEST_P(AdsIntegrationTest, RemoveWarmingListener) {
  initialize();

  // Send initial configuration to start workers, validate we can process a request.
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

  // Send a listener without its route, so it will be added as warming.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener,
      {buildListener("listener_0", "route_config_0"),
       buildListener("warming_listener_1", "nonexistent_route")},
      {buildListener("warming_listener_1", "nonexistent_route")}, {}, "2");
  test_server_->waitForGaugeEq("listener_manager.total_listeners_warming", 1);
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"nonexistent_route", "route_config_0"},
                                      {"nonexistent_route"}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "2", {}, {}, {}));

  // Send a request removing the warming listener.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {"warming_listener_1"}, "3");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                      {"route_config_0"}, {}, {"nonexistent_route"}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "3", {}, {}, {}));

  // The warming listener should be successfully removed.
  test_server_->waitForCounterEq("listener_manager.listener_removed", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_warming", 0);
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
      Config::TypeUrl::get().Cluster,
      {buildCluster("warming_cluster_1"), buildCluster("warming_cluster_2")},
      {buildCluster("warming_cluster_1"), buildCluster("warming_cluster_2")}, {}, "3");
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
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::adsBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ? "GRPC" : "DELTA_GRPC",
                                envoy::config::core::v3::ApiVersion::V3)) {
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
    setUpstreamProtocol(Http::CodecType::HTTP2);
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
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::adsBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ? "GRPC" : "DELTA_GRPC",
                                envoy::config::core::v3::ApiVersion::V3)) {
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
      eds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      eds_config->mutable_ads();
    });
    setUpstreamProtocol(Http::CodecType::HTTP2);
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
  envoy::service::discovery::v3::DiscoveryRequest sotw_request;
  envoy::service::discovery::v3::DeltaDiscoveryRequest delta_request;
  const envoy::config::core::v3::Node* node = nullptr;
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

TEST_P(AdsIntegrationTest, SetNodeAlways) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
    ads_config->set_set_node_on_first_message_only(false);
  });
  initialize();

  // Check that the node is sent in each request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}, true));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}, true));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}, true));
};

// Check if EDS cluster defined in file is loaded before ADS request and used as xDS server
class AdsClusterFromFileIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest,
                                          public HttpIntegrationTest {
public:
  AdsClusterFromFileIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::adsBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ? "GRPC" : "DELTA_GRPC",
                                envoy::config::core::v3::ApiVersion::V3)) {
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
      ConfigHelper::setHttp2(*ads_cluster);
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
      ads_cluster_eds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);

      // Add EDS static Cluster that uses ADS as config Source.
      auto* ads_eds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_eds_cluster->set_name("ads_eds_cluster");
      ads_eds_cluster->set_type(envoy::config::cluster::v3::Cluster::EDS);
      auto* eds_cluster_config = ads_eds_cluster->mutable_eds_cluster_config();
      auto* eds_config = eds_cluster_config->mutable_eds_config();
      eds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      eds_config->mutable_ads();
    });
    setUpstreamProtocol(Http::CodecType::HTTP2);
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
                                      {"ads_eds_cluster"}, {"ads_eds_cluster"}, {}, true));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("ads_eds_cluster")},
      {buildClusterLoadAssignment("ads_eds_cluster")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"ads_eds_cluster"}, {}, {}));
}

class AdsIntegrationTestWithRtds : public AdsIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* layered_runtime = bootstrap.mutable_layered_runtime();
      auto* layer = layered_runtime->add_layers();
      layer->set_name("foobar");
      auto* rtds_layer = layer->mutable_rtds_layer();
      rtds_layer->set_name("ads_rtds_layer");
      auto* rtds_config = rtds_layer->mutable_rtds_config();
      rtds_config->mutable_ads();
      rtds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);

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
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}));
    EXPECT_TRUE(
        compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "1", {"ads_rtds_layer"}, {}, {}));
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
  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add secondary cluster to the list of static resources.
      auto* eds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      eds_cluster->set_name("eds_cluster");
      eds_cluster->set_type(envoy::config::cluster::v3::Cluster::EDS);
      auto* eds_cluster_config = eds_cluster->mutable_eds_cluster_config();
      eds_cluster_config->mutable_eds_config()->mutable_ads();
      eds_cluster_config->mutable_eds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
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

// Node is resent on a dynamic context parameter update.
TEST_P(AdsIntegrationTest, ContextParameterUpdate) {
  initialize();

  // Check that the node is sent in each request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}, false));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}, false));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}, false));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}, false));

  // Set a Cluster DCP.
  test_server_->setDynamicContextParam(Config::TypeUrl::get().Cluster, "foo", "bar");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}, true));
  EXPECT_EQ("bar",
            last_node_.dynamic_parameters().at(Config::TypeUrl::get().Cluster).params().at("foo"));

  // Modify Cluster DCP.
  test_server_->setDynamicContextParam(Config::TypeUrl::get().Cluster, "foo", "baz");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}, true));
  EXPECT_EQ("baz",
            last_node_.dynamic_parameters().at(Config::TypeUrl::get().Cluster).params().at("foo"));

  // Modify CLA DCP (some other resource type URL).
  test_server_->setDynamicContextParam(Config::TypeUrl::get().ClusterLoadAssignment, "foo", "b");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_0"}, {}, {}, true));
  EXPECT_EQ("b", last_node_.dynamic_parameters()
                     .at(Config::TypeUrl::get().ClusterLoadAssignment)
                     .params()
                     .at("foo"));
  EXPECT_EQ("baz",
            last_node_.dynamic_parameters().at(Config::TypeUrl::get().Cluster).params().at("foo"));

  // Clear Cluster DCP.
  test_server_->unsetDynamicContextParam(Config::TypeUrl::get().Cluster, "foo");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}, true));
  EXPECT_EQ(
      0, last_node_.dynamic_parameters().at(Config::TypeUrl::get().Cluster).params().count("foo"));
}

class XdsTpAdsIntegrationTest : public AdsIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.add_node_context_params("id");
      bootstrap.add_node_context_params("cluster");
      bootstrap.mutable_dynamic_resources()->set_cds_resources_locator(
          "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/*");
      auto* cds_config = bootstrap.mutable_dynamic_resources()->mutable_cds_config();
      cds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      cds_config->mutable_api_config_source()->set_api_type(
          envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC);
      cds_config->mutable_api_config_source()->set_transport_api_version(
          envoy::config::core::v3::V3);
      bootstrap.mutable_dynamic_resources()->set_lds_resources_locator(
          "xdstp://test/envoy.config.listener.v3.Listener/foo-listener/*");
      auto* lds_config = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
      lds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      lds_config->mutable_api_config_source()->set_api_type(
          envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC);
      lds_config->mutable_api_config_source()->set_transport_api_version(
          envoy::config::core::v3::V3);
      auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
      ads_config->set_set_node_on_first_message_only(true);
    });
    AdsIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDelta, XdsTpAdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     // There should be no variation across clients.
                     testing::Values(Grpc::ClientType::EnvoyGrpc),
                     // Only delta xDS is supported for XdsTp
                     testing::Values(Grpc::SotwOrDelta::Delta)));

TEST_P(XdsTpAdsIntegrationTest, Basic) {
  initialize();
  // Basic CDS/EDS xDS initialization (CDS via xdstp:// glob collection).
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {},
                                      {"xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                       "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                                      {}, true));
  const std::string cluster_name = "xdstp://test/envoy.config.cluster.v3.Cluster/foo-cluster/"
                                   "baz?xds.node.cluster=cluster_name&xds.node.id=node_name";
  auto cluster_resource = buildCluster(cluster_name);
  const std::string endpoints_name =
      "xdstp://test/envoy.config.endpoint.v3.ClusterLoadAssignment/foo-cluster/baz";
  cluster_resource.mutable_eds_cluster_config()->set_service_name(endpoints_name);
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster, {},
                                                             {cluster_resource}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "", {},
                                      {endpoints_name}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {},
      {buildClusterLoadAssignment(endpoints_name)}, {}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}, {}, {}));

  // LDS/RDS xDS initialization (LDS via xdstp:// glob collection)
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {},
                              {"xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                               "*?xds.node.cluster=cluster_name&xds.node.id=node_name"},
                              {}));
  const std::string route_name_0 =
      "xdstp://test/envoy.config.route.v3.RouteConfiguration/route_config_0";
  const std::string route_name_1 =
      "xdstp://test/envoy.config.route.v3.RouteConfiguration/route_config_1";
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {},
      {
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
      },
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

  // Add a second listener in the foo namespace.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {},
      {buildListener("xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                     "baz?xds.node.cluster=cluster_name&xds.node.id=node_name",
                     route_name_1)},
      {}, "2");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1", {},
                                      {route_name_1}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {}, {buildRouteConfig(route_name_1, cluster_name)},
      {}, "2");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "2", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2", {}, {}, {}));
  test_server_->waitForCounterEq("listener_manager.listener_create_success", 2);
  makeSingleRequest();

  // Update bar listener in the foo namespace.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {},
      {buildListener("xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
                     "bar?xds.node.cluster=cluster_name&xds.node.id=node_name",
                     route_name_1)},
      {}, "3");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "3", {}, {}, {}));
  test_server_->waitForCounterEq("listener_manager.listener_in_place_updated", 1);
  makeSingleRequest();

  // Remove bar listener from the foo namespace.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, {}, {},
      {"xdstp://test/envoy.config.listener.v3.Listener/foo-listener/"
       "bar?xds.node.cluster=cluster_name&xds.node.id=node_name"},
      "3");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "4", {}, {}, {}));
  test_server_->waitForCounterEq("listener_manager.listener_removed", 1);
  makeSingleRequest();
}

// Some v2 ADS integration tests, these validate basic v2 support but are not complete, they reflect
// tests that have historically been worth validating on both v2 and v3. They will be removed in Q1.
class AdsClusterV2Test : public AdsIntegrationTest {
public:
  AdsClusterV2Test() : AdsIntegrationTest(envoy::config::core::v3::ApiVersion::V2) {}
  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster0->mutable_typed_extension_protocol_options()->clear();
      cluster0->mutable_http2_protocol_options();
    });
    AdsIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, AdsClusterV2Test,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Basic CDS/EDS update that warms and makes active a single cluster (v2 API).
TEST_P(AdsClusterV2Test, DEPRECATED_FEATURE_TEST(BasicClusterInitialWarming)) {
  initialize();
  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V2);
  const auto eds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      envoy::config::core::v3::ApiVersion::V2);

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("cluster_0")}, {buildCluster("cluster_0")}, {}, "1", true);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1", true);

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
}

// Verify CDS is paused during cluster warming.
TEST_P(AdsClusterV2Test, DEPRECATED_FEATURE_TEST(CdsPausedDuringWarming)) {
  initialize();

  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V2);
  const auto eds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      envoy::config::core::v3::ApiVersion::V2);
  const auto lds_type_url = Config::getTypeUrl<envoy::config::listener::v3::Listener>(
      envoy::config::core::v3::ApiVersion::V2);
  const auto rds_type_url = Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>(
      envoy::config::core::v3::ApiVersion::V2);

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("cluster_0")}, {buildCluster("cluster_0")}, {}, "1", true);
  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {"cluster_0"}, {"cluster_0"}, {}));

  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      eds_type_url, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1", true);

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(lds_type_url, "", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      lds_type_url, {buildListener("listener_0", "route_config_0")},
      {buildListener("listener_0", "route_config_0")}, {}, "1", true);

  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "1", {"cluster_0"}, {}, {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(rds_type_url, "", {"route_config_0"}, {"route_config_0"}, {}));
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      rds_type_url, {buildRouteConfig("route_config_0", "cluster_0")},
      {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1", true);

  EXPECT_TRUE(compareDiscoveryRequest(lds_type_url, "1", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(rds_type_url, "1", {"route_config_0"}, {}, {}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Send the first warming cluster.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("warming_cluster_1")}, {buildCluster("warming_cluster_1")},
      {"cluster_0"}, "2", true);

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);

  EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "1", {"warming_cluster_1"},
                                      {"warming_cluster_1"}, {"cluster_0"}));

  // Send the second warming cluster.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("warming_cluster_1"), buildCluster("warming_cluster_2")},
      {buildCluster("warming_cluster_1"), buildCluster("warming_cluster_2")}, {}, "3", true);
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
      {"cluster_0"}, "2", true);

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
TEST_P(AdsClusterV2Test, DEPRECATED_FEATURE_TEST(XdsBatching)) {
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
            envoy::config::core::v3::ApiVersion::V2);
    const auto rds_type_url = Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>(
        envoy::config::core::v3::ApiVersion::V2);

    EXPECT_TRUE(compareDiscoveryRequest(eds_type_url, "", {"eds_cluster2", "eds_cluster"},
                                        {"eds_cluster2", "eds_cluster"}, {}, true));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        eds_type_url,
        {buildClusterLoadAssignment("eds_cluster"), buildClusterLoadAssignment("eds_cluster2")},
        {buildClusterLoadAssignment("eds_cluster"), buildClusterLoadAssignment("eds_cluster2")}, {},
        "1", true);

    EXPECT_TRUE(compareDiscoveryRequest(rds_type_url, "", {"route_config2", "route_config"},
                                        {"route_config2", "route_config"}, {}));
    sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
        rds_type_url,
        {buildRouteConfig("route_config2", "eds_cluster2"),
         buildRouteConfig("route_config", "dummy_cluster")},
        {buildRouteConfig("route_config2", "eds_cluster2"),
         buildRouteConfig("route_config", "dummy_cluster")},
        {}, "1", true);
  };

  initialize();
}

// Regression test for https://github.com/envoyproxy/envoy/issues/13681.
TEST_P(AdsClusterV2Test, DEPRECATED_FEATURE_TEST(TypeUrlAnnotationRegression)) {
  initialize();
  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V2);

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  auto cluster = buildCluster("cluster_0");
  auto* bias = cluster.mutable_least_request_lb_config()->mutable_active_request_bias();
  bias->set_default_value(1.1);
  bias->set_runtime_key("foo");
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(cds_type_url, {cluster}, {cluster}, {},
                                                             "1", true);

  test_server_->waitForCounterGe("cluster_manager.cds.update_rejected", 1);
}

// Validate v2 resource are rejected by default.
class AdsV2ResourceRejectTest : public AdsIntegrationTest {
public:
  // We need to use a v3 transport as we're not going to set the v2 allow overrides.
  AdsV2ResourceRejectTest()
      : AdsIntegrationTest(envoy::config::core::v3::ApiVersion::V2,
                           envoy::config::core::v3::ApiVersion::V3) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, AdsV2ResourceRejectTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// If we attempt to use v2 APIs by default, the configuration should be rejected.
TEST_P(AdsV2ResourceRejectTest, DEPRECATED_FEATURE_TEST(RejectV2ConfigByDefault)) {
  fatal_by_default_v2_override_ = true;
  initialize();
  const auto cds_type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(
      envoy::config::core::v3::ApiVersion::V2);

  EXPECT_TRUE(compareDiscoveryRequest(cds_type_url, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      cds_type_url, {buildCluster("cluster_0")}, {buildCluster("cluster_0")}, {}, "1", true);
  test_server_->waitForCounterGe("cluster_manager.cds.update_rejected", 1);
  EXPECT_EQ(1, test_server_->gauge("runtime.deprecated_feature_seen_since_process_start")->value());
}

} // namespace Envoy
