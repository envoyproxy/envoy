#include "test/integration/ads_integration.h"

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

using testing::AssertionResult;

namespace Envoy {

AdsIntegrationTest::AdsIntegrationTest()
    : HttpIntegrationTest(
          Http::CodecType::HTTP2, ipVersion(),
          ConfigHelper::adsBootstrap((sotwOrDelta() == Grpc::SotwOrDelta::Sotw) ||
                                             (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw)
                                         ? "GRPC"
                                         : "DELTA_GRPC")) {
  // TODO(ggreenway): add tag extraction rules.
  // Missing stat tag-extraction rule for stat 'grpc.ads_cluster.streams_closed_9' and stat_prefix
  // 'ads_cluster'.
  skip_tag_extraction_rule_check_ = true;

  config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                    (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw ||
                                     sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta)
                                        ? "true"
                                        : "false");
  use_lds_ = false;
  create_xds_upstream_ = true;
  tls_xds_upstream_ = true;
  sotw_or_delta_ = sotwOrDelta();
  setUpstreamProtocol(Http::CodecType::HTTP2);
}

void AdsIntegrationTest::TearDown() { cleanUpXdsConnection(); }

envoy::config::cluster::v3::Cluster
AdsIntegrationTest::buildCluster(const std::string& name,
                                 envoy::config::cluster::v3::Cluster::LbPolicy lb_policy) {
  return ConfigHelper::buildCluster(name, lb_policy);
}

envoy::config::cluster::v3::Cluster AdsIntegrationTest::buildTlsCluster(const std::string& name) {
  return ConfigHelper::buildTlsCluster(name, envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
}

envoy::config::cluster::v3::Cluster AdsIntegrationTest::buildRedisCluster(const std::string& name) {
  return ConfigHelper::buildCluster(name, envoy::config::cluster::v3::Cluster::MAGLEV);
}

envoy::config::endpoint::v3::ClusterLoadAssignment
AdsIntegrationTest::buildClusterLoadAssignment(const std::string& name) {
  return ConfigHelper::buildClusterLoadAssignment(
      name, Network::Test::getLoopbackAddressString(ipVersion()),
      fake_upstreams_[0]->localAddress()->ip()->port());
}

envoy::config::endpoint::v3::ClusterLoadAssignment
AdsIntegrationTest::buildTlsClusterLoadAssignment(const std::string& name) {
  return ConfigHelper::buildClusterLoadAssignment(
      name, Network::Test::getLoopbackAddressString(ipVersion()), 8443);
}

envoy::config::endpoint::v3::ClusterLoadAssignment
AdsIntegrationTest::buildClusterLoadAssignmentWithLeds(const std::string& name,
                                                       const std::string& collection_name) {
  return ConfigHelper::buildClusterLoadAssignmentWithLeds(name, collection_name);
}

envoy::service::discovery::v3::Resource
AdsIntegrationTest::buildLbEndpointResource(const std::string& lb_endpoint_resource_name,
                                            const std::string& version) {
  envoy::service::discovery::v3::Resource resource;
  resource.set_name(lb_endpoint_resource_name);
  resource.set_version(version);

  envoy::config::endpoint::v3::LbEndpoint lb_endpoint =
      ConfigHelper::buildLbEndpoint(Network::Test::getLoopbackAddressString(ipVersion()),
                                    fake_upstreams_[0]->localAddress()->ip()->port());
  resource.mutable_resource()->PackFrom(lb_endpoint);
  return resource;
}

envoy::config::listener::v3::Listener
AdsIntegrationTest::buildListener(const std::string& name, const std::string& route_config,
                                  const std::string& stat_prefix) {
  return ConfigHelper::buildListener(
      name, route_config, Network::Test::getLoopbackAddressString(ipVersion()), stat_prefix);
}

envoy::config::listener::v3::Listener
AdsIntegrationTest::buildRedisListener(const std::string& name, const std::string& cluster) {
  std::string redis = fmt::format(
      R"EOF(
        filters:
        - name: redis
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.redis_proxy.v3.RedisProxy
            settings:
              op_timeout: 1s
            stat_prefix: {}
            prefix_routes:
              catch_all_route:
                cluster: {}
    )EOF",
      name, cluster);
  return ConfigHelper::buildBaseListener(name, Network::Test::getLoopbackAddressString(ipVersion()),
                                         redis);
}

envoy::config::route::v3::RouteConfiguration
AdsIntegrationTest::buildRouteConfig(const std::string& name, const std::string& cluster) {
  return ConfigHelper::buildRouteConfig(name, cluster);
}

void AdsIntegrationTest::makeSingleRequest() {
  registerTestServerPorts({"http"});
  testRouterHeaderOnlyRequestAndResponse();
  cleanupUpstreamAndDownstream();
}

void AdsIntegrationTest::initialize() { initializeAds(false); }

void AdsIntegrationTest::initializeAds(const bool rate_limiting) {
  config_helper_.addConfigModifier([this, &rate_limiting](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
    if (rate_limiting) {
      ads_config->mutable_rate_limit_settings();
    }
    auto* grpc_service = ads_config->add_grpc_services();
    setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
    auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
    ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    ads_cluster->set_name("ads_cluster");
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext context;
    auto* validation_context = context.mutable_common_tls_context()->mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    auto* san_matcher = validation_context->add_match_typed_subject_alt_names();
    san_matcher->mutable_matcher()->set_suffix("lyft.com");
    san_matcher->set_san_type(
        envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
    if (clientType() == Grpc::ClientType::GoogleGrpc) {
      auto* google_grpc = grpc_service->mutable_google_grpc();
      auto* ssl_creds = google_grpc->mutable_channel_credentials()->mutable_ssl_credentials();
      ssl_creds->mutable_root_certs()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    }
    ads_cluster->mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
    ads_cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(context);
  });
  HttpIntegrationTest::initialize();
  if (xds_stream_ == nullptr) {
    createXdsConnection();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }
}

void AdsIntegrationTest::testBasicFlow() {
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
  const ProtobufWkt::Timestamp first_active_listener_ts_1 =
      getListenersConfigDump().dynamic_listeners(0).active_state().last_updated();
  const ProtobufWkt::Timestamp first_active_cluster_ts_1 =
      getClustersConfigDump().dynamic_active_clusters()[0].last_updated();
  const ProtobufWkt::Timestamp first_route_config_ts_1 =
      getRoutesConfigDump().dynamic_route_configs()[0].last_updated();

  // Upgrade RDS/CDS/EDS to a newer config, validate we can process a request.
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("cluster_1"), buildCluster("cluster_2")},
      {buildCluster("cluster_1"), buildCluster("cluster_2")}, {"cluster_0"}, "2");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 2);
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
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
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_1")},
      {buildRouteConfig("route_config_0", "cluster_1")}, {}, "2");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2",
                                      {"route_config_0"}, {}, {}));

  makeSingleRequest();
  const ProtobufWkt::Timestamp first_active_listener_ts_2 =
      getListenersConfigDump().dynamic_listeners(0).active_state().last_updated();
  const ProtobufWkt::Timestamp first_active_cluster_ts_2 =
      getClustersConfigDump().dynamic_active_clusters()[0].last_updated();
  const ProtobufWkt::Timestamp first_route_config_ts_2 =
      getRoutesConfigDump().dynamic_route_configs()[0].last_updated();

  // Upgrade LDS/RDS, validate we can process a request.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener,
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
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
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
      getListenersConfigDump().dynamic_listeners(0).active_state().last_updated();
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

envoy::admin::v3::ClustersConfigDump AdsIntegrationTest::getClustersConfigDump() {
  auto message_ptr = test_server_->server().admin()->getConfigTracker().getCallbacksMap().at(
      "clusters")(Matchers::UniversalStringMatcher());
  return dynamic_cast<const envoy::admin::v3::ClustersConfigDump&>(*message_ptr);
}

envoy::admin::v3::ListenersConfigDump AdsIntegrationTest::getListenersConfigDump() {
  auto message_ptr = test_server_->server().admin()->getConfigTracker().getCallbacksMap().at(
      "listeners")(Matchers::UniversalStringMatcher());
  return dynamic_cast<const envoy::admin::v3::ListenersConfigDump&>(*message_ptr);
}

envoy::admin::v3::RoutesConfigDump AdsIntegrationTest::getRoutesConfigDump() {
  auto message_ptr = test_server_->server().admin()->getConfigTracker().getCallbacksMap().at(
      "routes")(Matchers::UniversalStringMatcher());
  return dynamic_cast<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
}

} // namespace Envoy
