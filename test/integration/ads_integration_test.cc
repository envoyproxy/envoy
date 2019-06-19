#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v2/ads.pb.h"
#include "envoy/stats/scope.h"

#include "common/config/protobuf_link_hacks.h"
#include "common/config/resources.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::IsSubstring;

namespace Envoy {
namespace {

const std::string config = R"EOF(
dynamic_resources:
  lds_config: {ads: {}}
  cds_config: {ads: {}}
  ads_config:
    api_type: GRPC
static_resources:
  clusters:
    name: dummy_cluster
    connect_timeout: { seconds: 5 }
    type: STATIC
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    lb_policy: ROUND_ROBIN
    http2_protocol_options: {}
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF";

class AdsIntegrationTest : public Grpc::GrpcClientIntegrationParamTest, public HttpIntegrationTest {
public:
  AdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), config) {
    use_lds_ = false;
    create_xds_upstream_ = true;
    tls_xds_upstream_ = true;
  }

  void TearDown() override {
    cleanUpXdsConnection();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  envoy::api::v2::Cluster buildCluster(const std::string& name) {
    return TestUtility::parseYaml<envoy::api::v2::Cluster>(fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: EDS
      eds_cluster_config: {{ eds_config: {{ ads: {{}} }} }}
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {{}}
    )EOF",
                                                                       name));
  }

  envoy::api::v2::ClusterLoadAssignment buildClusterLoadAssignment(const std::string& name) {
    return TestUtility::parseYaml<envoy::api::v2::ClusterLoadAssignment>(
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

  envoy::api::v2::Listener buildListener(const std::string& name, const std::string& route_config,
                                         const std::string& stat_prefix = "ads_test") {
    return TestUtility::parseYaml<envoy::api::v2::Listener>(fmt::format(
        R"EOF(
      name: {}
      address:
        socket_address:
          address: {}
          port_value: 0
      filter_chains:
        filters:
        - name: envoy.http_connection_manager
          config:
            stat_prefix: {}
            codec_type: HTTP2
            rds:
              route_config_name: {}
              config_source: {{ ads: {{}} }}
            http_filters: [{{ name: envoy.router }}]
    )EOF",
        name, Network::Test::getLoopbackAddressString(ipVersion()), stat_prefix, route_config));
  }

  envoy::api::v2::RouteConfiguration buildRouteConfig(const std::string& name,
                                                      const std::string& cluster) {
    return TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(fmt::format(R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
    )EOF",
                                                                                  name, cluster));
  }

  void makeSingleRequest() {
    registerTestServerPorts({"http"});
    testRouterHeaderOnlyRequestAndResponse();
    cleanupUpstreamAndDownstream();
  }

  void initialize() override { initializeAds(false); }

  void initializeAds(const bool rate_limiting) {
    config_helper_.addConfigModifier(
        [this, &rate_limiting](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
          auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
          if (rate_limiting) {
            ads_config->mutable_rate_limit_settings();
          }
          auto* grpc_service = ads_config->add_grpc_services();
          setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
          auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ads_cluster->set_name("ads_cluster");
          auto* context = ads_cluster->mutable_tls_context();
          auto* validation_context =
              context->mutable_common_tls_context()->mutable_validation_context();
          validation_context->mutable_trusted_ca()->set_filename(
              TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
          validation_context->add_verify_subject_alt_name("foo.lyft.com");
          if (clientType() == Grpc::ClientType::GoogleGrpc) {
            auto* google_grpc = grpc_service->mutable_google_grpc();
            auto* ssl_creds = google_grpc->mutable_channel_credentials()->mutable_ssl_credentials();
            ssl_creds->mutable_root_certs()->set_filename(
                TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
          }
        });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    HttpIntegrationTest::initialize();
    if (xds_stream_ == nullptr) {
      createXdsConnection();
      AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();
    }
  }

  void testBasicFlow() {
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
        Config::TypeUrl::get().RouteConfiguration,
        {buildRouteConfig("route_config_0", "cluster_0")},
        {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1",
                                        {"route_config_0"}, {}, {}));

    test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
    makeSingleRequest();
    const ProtobufWkt::Timestamp first_active_listener_ts_1 =
        getListenersConfigDump().dynamic_active_listeners()[0].last_updated();
    const ProtobufWkt::Timestamp first_active_cluster_ts_1 =
        getClustersConfigDump().dynamic_active_clusters()[0].last_updated();
    const ProtobufWkt::Timestamp first_route_config_ts_1 =
        getRoutesConfigDump().dynamic_route_configs()[0].last_updated();

    // Upgrade RDS/CDS/EDS to a newer config, validate we can process a request.
    sendDiscoveryResponse<envoy::api::v2::Cluster>(
        Config::TypeUrl::get().Cluster, {buildCluster("cluster_1"), buildCluster("cluster_2")},
        {buildCluster("cluster_1"), buildCluster("cluster_2")}, {"cluster_0"}, "2");
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 2);
    sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment,
        {buildClusterLoadAssignment("cluster_1"), buildClusterLoadAssignment("cluster_2")},
        {buildClusterLoadAssignment("cluster_1"), buildClusterLoadAssignment("cluster_2")},
        {"cluster_0"}, "2");
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                        {"cluster_2", "cluster_1"}, {"cluster_2", "cluster_1"},
                                        {"cluster_0"}));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "2", {}, {}, {}));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "2",
                                        {"cluster_2", "cluster_1"}, {}, {}));
    sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
        Config::TypeUrl::get().RouteConfiguration,
        {buildRouteConfig("route_config_0", "cluster_1")},
        {buildRouteConfig("route_config_0", "cluster_1")}, {}, "2");
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2",
                                        {"route_config_0"}, {}, {}));

    makeSingleRequest();
    const ProtobufWkt::Timestamp first_active_listener_ts_2 =
        getListenersConfigDump().dynamic_active_listeners()[0].last_updated();
    const ProtobufWkt::Timestamp first_active_cluster_ts_2 =
        getClustersConfigDump().dynamic_active_clusters()[0].last_updated();
    const ProtobufWkt::Timestamp first_route_config_ts_2 =
        getRoutesConfigDump().dynamic_route_configs()[0].last_updated();

    // Upgrade LDS/RDS, validate we can process a request.
    sendDiscoveryResponse<envoy::api::v2::Listener>(Config::TypeUrl::get().Listener,
                                                    {buildListener("listener_1", "route_config_1"),
                                                     buildListener("listener_2", "route_config_2")},
                                                    {buildListener("listener_1", "route_config_1"),
                                                     buildListener("listener_2", "route_config_2")},
                                                    {"listener_0"}, "2");
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2",
                                        {"route_config_2", "route_config_1", "route_config_0"},
                                        {"route_config_2", "route_config_1"}, {}));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "2", {}, {}, {}));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2",
                                        {"route_config_2", "route_config_1"}, {},
                                        {"route_config_0"}));
    sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
        Config::TypeUrl::get().RouteConfiguration,
        {buildRouteConfig("route_config_1", "cluster_1"),
         buildRouteConfig("route_config_2", "cluster_1")},
        {buildRouteConfig("route_config_1", "cluster_1"),
         buildRouteConfig("route_config_2", "cluster_1")},
        {"route_config_0"}, "3");
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "3",
                                        {"route_config_2", "route_config_1"}, {}, {}));

    test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
    makeSingleRequest();
    const ProtobufWkt::Timestamp first_active_listener_ts_3 =
        getListenersConfigDump().dynamic_active_listeners()[0].last_updated();
    const ProtobufWkt::Timestamp first_active_cluster_ts_3 =
        getClustersConfigDump().dynamic_active_clusters()[0].last_updated();
    const ProtobufWkt::Timestamp first_route_config_ts_3 =
        getRoutesConfigDump().dynamic_route_configs()[0].last_updated();

    // Expect last_updated timestamps to be updated in a predictable way
    // For the listener configs in this example, 1 == 2 < 3.
    EXPECT_EQ(first_active_listener_ts_2, first_active_listener_ts_1);
    EXPECT_GT(first_active_listener_ts_3, first_active_listener_ts_2);
    // For the cluster configs in this example, 1 < 2 == 3.
    EXPECT_GT(first_active_cluster_ts_2, first_active_cluster_ts_1);
    EXPECT_EQ(first_active_cluster_ts_3, first_active_cluster_ts_2);
    // For the route configs in this example, 1 < 2 < 3.
    EXPECT_GT(first_route_config_ts_2, first_route_config_ts_1);
    EXPECT_GT(first_route_config_ts_3, first_route_config_ts_2);
  }

  envoy::admin::v2alpha::ClustersConfigDump getClustersConfigDump() {
    auto message_ptr =
        test_server_->server().admin().getConfigTracker().getCallbacksMap().at("clusters")();
    return dynamic_cast<const envoy::admin::v2alpha::ClustersConfigDump&>(*message_ptr);
  }

  envoy::admin::v2alpha::ListenersConfigDump getListenersConfigDump() {
    auto message_ptr =
        test_server_->server().admin().getConfigTracker().getCallbacksMap().at("listeners")();
    return dynamic_cast<const envoy::admin::v2alpha::ListenersConfigDump&>(*message_ptr);
  }

  envoy::admin::v2alpha::RoutesConfigDump getRoutesConfigDump() {
    auto message_ptr =
        test_server_->server().admin().getConfigTracker().getCallbacksMap().at("routes")();
    return dynamic_cast<const envoy::admin::v2alpha::RoutesConfigDump&>(*message_ptr);
  }

  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_{timeSystem()};
};

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
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), config) {
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
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), config) {
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

} // namespace
} // namespace Envoy
