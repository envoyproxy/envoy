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
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"
#include "common/ssl/ssl_socket.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/network_utility.h"
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

class AdsIntegrationTest : public HttpIntegrationTest, public Grpc::GrpcClientIntegrationParamTest {
public:
  AdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), config) {}

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(createUpstreamSslContext(), 0, FakeHttpConnection::Type::HTTP2, version_));
  }

  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::api::v2::auth::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    common_tls_context->add_alpn_protocols("h2");
    auto* tls_cert = common_tls_context->add_tls_certificates();
    tls_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
    tls_cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
    Ssl::ServerContextConfigImpl cfg(tls_context);

    static Stats::Scope* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
    return std::make_unique<Ssl::ServerSslSocketFactory>(
        cfg, context_manager_, *upstream_stats_store, std::vector<std::string>{});
  }

  AssertionResult
  compareDiscoveryRequest(const std::string& expected_type_url, const std::string& expected_version,
                          const std::vector<std::string>& expected_resource_names,
                          const Protobuf::int32 expected_error_code = Grpc::Status::GrpcStatus::Ok,
                          const std::string& expected_error_message = "") {
    envoy::api::v2::DiscoveryRequest discovery_request;
    ads_stream_->waitForGrpcMessage(*dispatcher_, discovery_request);

    // TODO(PiotrSikora): Remove this hack once fixed internally.
    if (!(expected_type_url == discovery_request.type_url())) {
      return AssertionFailure() << fmt::format("type_url {} does not match expected {}",
                                               discovery_request.type_url(), expected_type_url);
    }
    if (!(expected_error_code == discovery_request.error_detail().code())) {
      return AssertionFailure() << fmt::format("error_code {} does not match expected {}",
                                               discovery_request.error_detail().code(),
                                               expected_error_code);
    }
    EXPECT_TRUE(
        IsSubstring("", "", expected_error_message, discovery_request.error_detail().message()));
    const std::vector<std::string> resource_names(discovery_request.resource_names().cbegin(),
                                                  discovery_request.resource_names().cend());
    if (expected_resource_names != resource_names) {
      return AssertionFailure() << fmt::format(
                 "resources {} do not match expected {} in {}",
                 fmt::join(resource_names.begin(), resource_names.end(), ","),
                 fmt::join(expected_resource_names.begin(), expected_resource_names.end(), ","),
                 discovery_request.DebugString());
    }
    // TODO(PiotrSikora): Remove this hack once fixed internally.
    if (!(expected_version == discovery_request.version_info())) {
      return AssertionFailure() << fmt::format("version {} does not match expected {} in {}",
                                               discovery_request.version_info(), expected_version,
                                               discovery_request.DebugString());
    }
    return AssertionSuccess();
  }

  template <class T>
  void sendDiscoveryResponse(const std::string& type_url, const std::vector<T>& messages,
                             const std::string& version) {
    envoy::api::v2::DiscoveryResponse discovery_response;
    discovery_response.set_version_info(version);
    discovery_response.set_type_url(type_url);
    for (const auto& message : messages) {
      discovery_response.add_resources()->PackFrom(message);
    }
    ads_stream_->sendGrpcMessage(discovery_response);
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

  envoy::api::v2::Listener buildListener(const std::string& name, const std::string& route_config) {
    return TestUtility::parseYaml<envoy::api::v2::Listener>(
        fmt::format(R"EOF(
      name: {}
      address:
        socket_address:
          address: {}
          port_value: 0
      filter_chains:
        filters:
        - name: envoy.http_connection_manager
          config:
            stat_prefix: ads_test
            codec_type: HTTP2
            rds:
              route_config_name: {}
              config_source: {{ ads: {{}} }}
            http_filters: [{{ name: envoy.router }}]
    )EOF",
                    name, Network::Test::getLoopbackAddressString(ipVersion()), route_config));
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
    testRouterHeaderOnlyRequestAndResponse(true);
    cleanupUpstreamAndDownstream();
    fake_upstream_connection_ = nullptr;
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* grpc_service =
          bootstrap.mutable_dynamic_resources()->mutable_ads_config()->add_grpc_services();
      setGrpcService(*grpc_service, "ads_cluster", fake_upstreams_.back()->localAddress());
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
    if (ads_stream_ == nullptr) {
      ads_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
      ads_stream_ = ads_connection_->waitForNewStream(*dispatcher_);
      ads_stream_->startGrpcStream();
    }
  }

  Runtime::MockLoader runtime_;
  Ssl::ContextManagerImpl context_manager_{runtime_};
  FakeHttpConnectionPtr ads_connection_;
  FakeStreamPtr ads_stream_;
};

INSTANTIATE_TEST_CASE_P(IpVersionsClientType, AdsIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

// Validate basic config delivery and upgrade.
TEST_P(AdsIntegrationTest, Basic) {
  initialize();

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")}, "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "", {"cluster_0"}));
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}));
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")}, "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1", {"cluster_0"}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "", {"route_config_0"}));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1", {"route_config_0"}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Upgrade RDS/CDS/EDS to a newer config, validate we can process a request.
  sendDiscoveryResponse<envoy::api::v2::Cluster>(
      Config::TypeUrl::get().Cluster, {buildCluster("cluster_1"), buildCluster("cluster_2")}, "2");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 2);
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment("cluster_1"), buildClusterLoadAssignment("cluster_2")}, "2");
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_2", "cluster_1"}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "2", {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "2",
                                      {"cluster_2", "cluster_1"}));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_1")},
      "2");
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2", {"route_config_0"}));

  makeSingleRequest();

  // Upgrade LDS/RDS, validate we can process a request.
  sendDiscoveryResponse<envoy::api::v2::Listener>(Config::TypeUrl::get().Listener,
                                                  {buildListener("listener_1", "route_config_1"),
                                                   buildListener("listener_2", "route_config_2")},
                                                  "2");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2",
                                      {"route_config_2", "route_config_1", "route_config_0"}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "2", {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2",
                                      {"route_config_2", "route_config_1"}));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration,
      {buildRouteConfig("route_config_1", "cluster_1"),
       buildRouteConfig("route_config_2", "cluster_1")},
      "3");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "3",
                                      {"route_config_2", "route_config_1"}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
  makeSingleRequest();
}

// Validate that we can recover from failures.
TEST_P(AdsIntegrationTest, Failure) {
  initialize();

  // Send initial configuration, failing each xDS once (via a type mismatch), validate we can
  // process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}));
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().Cluster, {buildClusterLoadAssignment("cluster_0")}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}));

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().Cluster, "", {}, Grpc::Status::GrpcStatus::Internal,
      fmt::format("{} does not match {}", Config::TypeUrl::get().ClusterLoadAssignment,
                  Config::TypeUrl::get().Cluster)));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")}, "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "", {"cluster_0"}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().ClusterLoadAssignment,
                                                 {buildCluster("cluster_0")}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "", {"cluster_0"},
                              Grpc::Status::GrpcStatus::Internal,
                              fmt::format("{} does not match {}", Config::TypeUrl::get().Cluster,
                                          Config::TypeUrl::get().ClusterLoadAssignment)));
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")}, "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1", {"cluster_0"}));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().Listener, {buildRouteConfig("listener_0", "route_config_0")}, "1");

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().Listener, "", {}, Grpc::Status::GrpcStatus::Internal,
      fmt::format("{} does not match {}", Config::TypeUrl::get().RouteConfiguration,
                  Config::TypeUrl::get().Listener)));
  sendDiscoveryResponse<envoy::api::v2::Listener>(
      Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")}, "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "", {"route_config_0"}));
  sendDiscoveryResponse<envoy::api::v2::Listener>(Config::TypeUrl::get().RouteConfiguration,
                                                  {buildListener("route_config_0", "cluster_0")},
                                                  "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "", {"route_config_0"},
                              Grpc::Status::GrpcStatus::Internal,
                              fmt::format("{} does not match {}", Config::TypeUrl::get().Listener,
                                          Config::TypeUrl::get().RouteConfiguration)));
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, {buildRouteConfig("route_config_0", "cluster_0")},
      "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1", {"route_config_0"}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();
}

class AdsFailIntegrationTest : public HttpIntegrationTest,
                               public Grpc::GrpcClientIntegrationParamTest {
public:
  AdsFailIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), config) {}

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* grpc_service =
          bootstrap.mutable_dynamic_resources()->mutable_ads_config()->add_grpc_services();
      setGrpcService(*grpc_service, "ads_cluster", fake_upstreams_.back()->localAddress());
      auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ads_cluster->set_name("ads_cluster");
    });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    HttpIntegrationTest::initialize();
  }

  FakeHttpConnectionPtr ads_connection_;
  FakeStreamPtr ads_stream_;
};

INSTANTIATE_TEST_CASE_P(IpVersionsClientType, AdsFailIntegrationTest,
                        GRPC_CLIENT_INTEGRATION_PARAMS);

// Validate that we don't crash on failed ADS stream.
TEST_P(AdsFailIntegrationTest, ConnectDisconnect) {
  initialize();
  ads_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
  ads_stream_ = ads_connection_->waitForNewStream(*dispatcher_);
  ads_stream_->startGrpcStream();
  ads_stream_->finishGrpcStream(Grpc::Status::Internal);
}

class AdsConfigIntegrationTest : public HttpIntegrationTest,
                                 public Grpc::GrpcClientIntegrationParamTest {
public:
  AdsConfigIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), config) {}

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* grpc_service =
          bootstrap.mutable_dynamic_resources()->mutable_ads_config()->add_grpc_services();
      setGrpcService(*grpc_service, "ads_cluster", fake_upstreams_.back()->localAddress());
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

  FakeHttpConnectionPtr ads_connection_;
  FakeStreamPtr ads_stream_;
};

INSTANTIATE_TEST_CASE_P(IpVersionsClientType, AdsConfigIntegrationTest,
                        GRPC_CLIENT_INTEGRATION_PARAMS);

// This is s regression validating that we don't crash on EDS static Cluster that uses ADS.
TEST_P(AdsConfigIntegrationTest, EdsClusterWithAdsConfigSource) {
  initialize();
  ads_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
  ads_stream_ = ads_connection_->waitForNewStream(*dispatcher_);
  ads_stream_->startGrpcStream();
  ads_stream_->finishGrpcStream(Grpc::Status::Ok);
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

  pre_worker_start_test_steps_ = [this]() {
    ads_connection_ = fake_upstreams_.back()->waitForHttpConnection(*dispatcher_);
    ads_stream_ = ads_connection_->waitForNewStream(*dispatcher_);
    ads_stream_->startGrpcStream();

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                        {"eds_cluster2", "eds_cluster"}));
    sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
        Config::TypeUrl::get().ClusterLoadAssignment,
        {buildClusterLoadAssignment("eds_cluster"), buildClusterLoadAssignment("eds_cluster1")},
        "1");

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                        {"route_config2", "route_config"}));
    sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
        Config::TypeUrl::get().RouteConfiguration,
        {buildRouteConfig("route_config2", "eds_cluster2"),
         buildRouteConfig("route_config", "dummy_cluster")},
        "1");
  };

  initialize();
}

} // namespace
} // namespace Envoy
