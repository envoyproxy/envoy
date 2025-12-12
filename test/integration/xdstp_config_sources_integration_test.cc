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

// Tests for xDS-TP based config sources (defined in the bootstrap), without the
// ads_config definition.
class XdsTpConfigsIntegrationTest : public AdsDeltaSotwIntegrationSubStateParamTest,
                                    public HttpIntegrationTest {
public:
  XdsTpConfigsIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::httpProxyConfig(false)) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw ||
                                       sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta)
                                          ? "true"
                                          : "false");
    // Not using the normal xds upstream, but the
    // authority1_upstream_/default_upstream.
    create_xds_upstream_ = false;
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

  void TearDown() override {
    cleanupXdsConnection(authority1_xds_connection_);
    cleanupXdsConnection(default_authority_xds_connection_);
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // An upstream for authority1 (H/2), an upstream for the default_authority (H/2), and an
    // upstream for a backend (H/1).
    authority1_upstream_ = createAdsUpstream();
    default_authority_upstream_ = createAdsUpstream();
    addFakeUpstream(Http::CodecType::HTTP1);
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
    });
    HttpIntegrationTest::initialize();
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

  void cleanupXdsConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name) {
    // The last fake upstream is the emulated server.
    return ConfigHelper::buildClusterLoadAssignment(
        name, Network::Test::getLoopbackAddressString(ipVersion()),
        fake_upstreams_.back().get()->localAddress()->ip()->port());
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

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeltaWildcard, XdsTpConfigsIntegrationTest,
                         ADS_INTEGRATION_PARAMS);

// Validate that a bootstrap cluster that has an xds-tp based config EDS source
// works.
TEST_P(XdsTpConfigsIntegrationTest, EdsOnlyConfigAuthority1) {
  // Setup a static cluster that requires EDS from authority1.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();

    // Add an EDS cluster that will fetch endpoints from authority1.
    static_resources->mutable_clusters()->Add()->CopyFrom(
        TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(
            R"EOF(
                name: xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1
                type: EDS
                eds_cluster_config: {}
            )EOF"));
  });

  // Update the route to the xdstp-based cluster.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->set_cluster("xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
                          "clusters/cluster1");
      });

  // Envoy will request the endpoints of the cluster in the bootstrap during the
  // initialization phase. This will make sure the xDS server answers with the
  // correct assignment.
  on_server_init_function_ = [this]() {
    connectAuthority1();
    connectDefaultAuthority();

    // Authority1 should receive the EDS request.
    EXPECT_TRUE(compareDiscoveryRequest(
        Config::TypeUrl::get().ClusterLoadAssignment, "",
        {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
        {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
        {}, true, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment,
        {buildClusterLoadAssignment(
            "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
            "cluster1")},
        {buildClusterLoadAssignment(
            "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
            "cluster1")},
        {}, "1", {}, authority1_xds_stream_.get());

    // Expect an EDS ACK.
    EXPECT_TRUE(compareDiscoveryRequest(
        Config::TypeUrl::get().ClusterLoadAssignment, "1",
        {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
        {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  };

  initialize();

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Try to send a request and see that it reaches the backend (backend 3).
  testRouterHeaderOnlyRequestAndResponse(nullptr, 3);
  cleanupUpstreamAndDownstream();
}

// Validate that a bootstrap cluster that has an xds-tp based config EDS source
// that is dynamically updated works.
TEST_P(XdsTpConfigsIntegrationTest, EdsOnlyConfigAuthority1Update) {
  // Setup a static cluster that requires EDS from authority1.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();

    // Add an EDS cluster that will fetch endpoints from authority1.
    static_resources->mutable_clusters()->Add()->CopyFrom(
        TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(
            R"EOF(
                name: xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1
                type: EDS
                eds_cluster_config: {}
            )EOF"));
  });

  // Update the route to the xdstp-based cluster.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->set_cluster("xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
                          "clusters/cluster1");
      });

  // Envoy will request the endpoints of the cluster in the bootstrap during the
  // initialization phase. This will make sure the xDS server answers with the
  // correct assignment.
  on_server_init_function_ = [this]() {
    connectAuthority1();
    connectDefaultAuthority();

    // Authority1 should receive the EDS request.
    EXPECT_TRUE(compareDiscoveryRequest(
        Config::TypeUrl::get().ClusterLoadAssignment, "",
        {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
        {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
        {}, true, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));

    auto cla = buildClusterLoadAssignment(
        "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1");
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment, {cla}, {cla}, {}, "1", {},
        authority1_xds_stream_.get());

    // Expect an EDS ACK.
    EXPECT_TRUE(compareDiscoveryRequest(
        Config::TypeUrl::get().ClusterLoadAssignment, "1",
        {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
        {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  };

  initialize();

  test_server_->waitForCounterEq(
      "cluster.xdstp_authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
      "cluster1.update_success",
      1);

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Try to send a request and see that it reaches the backend (backend 3).
  testRouterHeaderOnlyRequestAndResponse(nullptr, 3);
  cleanupUpstreamAndDownstream();

  // Rebuild the same assignment as done in `on_server_init_function_` above.
  // This is done here because the endpoint address is unknown prior to that
  // function.
  auto cla = buildClusterLoadAssignment(
      "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1");
  // Send an update to the load-assignment.
  cla.mutable_endpoints(0)->mutable_locality()->set_sub_zone("new_sub_zone");
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {cla}, {cla}, {}, "2", {},
      authority1_xds_stream_.get());

  // Expect an EDS ACK.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().ClusterLoadAssignment, "2",
      {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
      {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));

  test_server_->waitForCounterEq(
      "cluster.xdstp_authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
      "cluster1.update_success",
      2);
}

// Validate that a bootstrap cluster that has an xds-tp based config EDS source
// that points to the default_config_source works.
TEST_P(XdsTpConfigsIntegrationTest, EdsOnlyConfigDefaultSource) {
  // Setup a static cluster that requires EDS from the default config source.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();
    // Add an EDS cluster that will fetch endpoints from the default config
    // source.
    static_resources->mutable_clusters()->Add()->CopyFrom(
        TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(
            R"EOF(
                name: xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1
                type: EDS
                eds_cluster_config: {}
            )EOF"));
  });

  // Update the route to the xdstp-based cluster.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->set_cluster("xdstp://default_authority.com/"
                          "envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1");
      });

  // Envoy will request the endpoints of the cluster in the bootstrap during the
  // initialization phase. This will make sure the xDS server answers with the
  // correct assignment.
  on_server_init_function_ = [this]() {
    connectAuthority1();
    connectDefaultAuthority();

    // Default Authority should receive the EDS request.
    EXPECT_TRUE(compareDiscoveryRequest(
        Config::TypeUrl::get().ClusterLoadAssignment, "",
        {"xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
         "cluster1"},
        {"xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
         "cluster1"},
        {}, true, Grpc::Status::WellKnownGrpcStatus::Ok, "", default_authority_xds_stream_.get()));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment,
        {buildClusterLoadAssignment(
            "xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
            "cluster1")},
        {buildClusterLoadAssignment(
            "xdstp://default_authority.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/"
            "cluster1")},
        {}, "1", {}, default_authority_xds_stream_.get());

    // Expect an EDS ACK.
    EXPECT_TRUE(compareDiscoveryRequest(
        Config::TypeUrl::get().ClusterLoadAssignment, "1",
        {"xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/clusters/cluster1"},
        {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
        default_authority_xds_stream_.get()));
  };

  initialize();

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Try to send a request and see that it reaches the backend (backend 3).
  testRouterHeaderOnlyRequestAndResponse(nullptr, 3);
  cleanupUpstreamAndDownstream();
}

// TODO(adisuissa): add a test that validates that two clusters with the same
// config source multiplex the request on the same stream.
} // namespace Envoy
