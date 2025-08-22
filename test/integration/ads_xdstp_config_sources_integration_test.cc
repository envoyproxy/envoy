#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/grpc/status.h"

#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"
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

// Tests for cases where both (old) ADS and xDS-TP based config sources are
// defined in the bootstrap.
class AdsXdsTpConfigsIntegrationTest : public AdsDeltaSotwIntegrationSubStateParamTest,
                                       public HttpIntegrationTest {
public:
  AdsXdsTpConfigsIntegrationTest()
      : HttpIntegrationTest(
            Http::CodecType::HTTP2, ipVersion(),
            // ConfigHelper::httpProxyConfig(false)) {
            ConfigHelper::adsBootstrap((sotwOrDelta() == Grpc::SotwOrDelta::Sotw) ||
                                               (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw)
                                           ? "GRPC"
                                           : "DELTA_GRPC")) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw ||
                                       sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta)
                                          ? "true"
                                          : "false");
    use_lds_ = false;
    // xds_upstream_ will be used for the ADS upstream.
    create_xds_upstream_ = true;
    // Not testing TLS in this case.
    tls_xds_upstream_ = false;
    sotw_or_delta_ = sotwOrDelta();
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  FakeUpstream* createAdsUpstream() {
    ASSERT(!tls_xds_upstream_);
    addFakeUpstream(Http::CodecType::HTTP2);
    return fake_upstreams_.back().get();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // An upstream for authority1 (H/2), an upstream for the default_authority (H/2), and an
    // upstream for a backend (H/1).
    authority1_upstream_ = createAdsUpstream();
    default_authority_upstream_ = createAdsUpstream();
  }

  void TearDown() override {
    cleanupXdsConnection(xds_connection_);
    cleanupXdsConnection(authority1_xds_connection_);
    cleanupXdsConnection(default_authority_xds_connection_);
  }

  void cleanupXdsConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  bool isSotw() const {
    return sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
           sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw;
  }

  // Adds config_source for authority1.com and a default_config_source for
  // default_authority.com.
  void initialize() override {
    config_helper_.addRuntimeOverride(
        "envoy.reloadable_features.xdstp_based_config_singleton_subscriptions", "true");
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add the first config_source.
      {
        auto* config_source1 = bootstrap.mutable_config_sources()->Add();
        config_source1->mutable_authorities()->Add()->set_name("authority1.com");
        auto* api_config_source = config_source1->mutable_api_config_source();
        api_config_source->set_api_type(
            isSotw() ? envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC
                     : envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC);
        api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
        api_config_source->set_set_node_on_first_message_only(true);
        auto* grpc_service = api_config_source->add_grpc_services();
        setGrpcService(*grpc_service, "authority1_cluster", authority1_upstream_->localAddress());
        auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
        xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        xds_cluster->set_name("authority1_cluster");
      }
      // Add the default config source.
      {
        auto* default_config_source = bootstrap.mutable_default_config_source();
        default_config_source->mutable_authorities()->Add()->set_name("default_authority.com");
        auto* api_config_source = default_config_source->mutable_api_config_source();
        api_config_source->set_api_type(
            isSotw() ? envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC
                     : envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC);
        api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
        api_config_source->set_set_node_on_first_message_only(true);
        auto* grpc_service = api_config_source->add_grpc_services();
        setGrpcService(*grpc_service, "default_authority_cluster",
                       default_authority_upstream_->localAddress());
        auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
        xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        xds_cluster->set_name("default_authority_cluster");
      }
      // Add the (old) ADS server.
      {
        auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
        ads_config->set_api_type(isSotw() ? envoy::config::core::v3::ApiConfigSource::GRPC
                                          : envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
        ads_config->set_transport_api_version(envoy::config::core::v3::V3);
        ads_config->set_set_node_on_first_message_only(true);
        auto* grpc_service = ads_config->add_grpc_services();
        setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
        auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
        ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        ads_cluster->set_name("ads_cluster");
      }
    });
    HttpIntegrationTest::initialize();
    connectAds();
    connectAuthority1();
    connectDefaultAuthority();
  }

  void connectAds() {
    if (xds_stream_ == nullptr) {
      AssertionResult result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
      RELEASE_ASSERT(result, result.message());
      result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();
    }
  }

  void connectAuthority1() {
    AssertionResult result =
        authority1_upstream_->waitForHttpConnection(*dispatcher_, authority1_xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = authority1_xds_connection_->waitForNewStream(*dispatcher_, authority1_xds_stream_);
    RELEASE_ASSERT(result, result.message());
    authority1_xds_stream_->startGrpcStream();
  }

  void connectDefaultAuthority() {
    AssertionResult result = default_authority_upstream_->waitForHttpConnection(
        *dispatcher_, default_authority_xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = default_authority_xds_connection_->waitForNewStream(*dispatcher_,
                                                                 default_authority_xds_stream_);
    RELEASE_ASSERT(result, result.message());
    default_authority_xds_stream_->startGrpcStream();
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name) {
    // The first fake upstream is the emulated backend server.
    return ConfigHelper::buildClusterLoadAssignment(
        name, Network::Test::getLoopbackAddressString(ipVersion()),
        fake_upstreams_[0].get()->localAddress()->ip()->port());
  }

  void setupClustersFromOldAds() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->mutable_ads();
    });
  }

  void setupListenersFromOldAds() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_dynamic_resources()->mutable_lds_config()->mutable_ads();
    });
  }

  envoy::config::listener::v3::Listener buildListener(const std::string& name,
                                                      const std::string& route_config) {
    return ConfigHelper::buildListener(
        name, route_config, Network::Test::getLoopbackAddressString(ipVersion()), "ads_test");
  }

  void makeSingleRequest() {
    registerTestServerPorts({"http"});
    testRouterHeaderOnlyRequestAndResponse();
    cleanupUpstreamAndDownstream();
  }

  // Data members that emulate the authority1 server.
  FakeUpstream* authority1_upstream_;
  FakeHttpConnectionPtr authority1_xds_connection_;
  FakeStreamPtr authority1_xds_stream_;

  // Data members that emulate the default_authority server.
  FakeUpstream* default_authority_upstream_;
  FakeHttpConnectionPtr default_authority_xds_connection_;
  FakeStreamPtr default_authority_xds_stream_;
};

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
