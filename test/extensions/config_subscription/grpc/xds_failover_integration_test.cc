#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/common/logger.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"
#include "source/common/tls/ssl_socket.h"

#include "test/integration/ads_integration.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {

namespace {
const auto CdsTypeUrl = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
const auto EdsTypeUrl = Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>();
const auto LdsTypeUrl = Config::getTypeUrl<envoy::config::listener::v3::Listener>();
} // namespace

// Tests the use of Envoy with a primary and failover sources.
class XdsFailoverAdsIntegrationTest : public AdsDeltaSotwIntegrationSubStateParamTest,
                                      public HttpIntegrationTest {
public:
  XdsFailoverAdsIntegrationTest()
      : HttpIntegrationTest(
            Http::CodecType::HTTP2, ipVersion(),
            ConfigHelper::adsBootstrap((sotwOrDelta() == Grpc::SotwOrDelta::Sotw) ||
                                               (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw)
                                           ? "GRPC"
                                           : "DELTA_GRPC")) {
    config_helper_.addRuntimeOverride("envoy.restart_features.xds_failover_support", "true");
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

  void TearDown() override {
    cleanUpConnection(failover_xds_connection_);
    cleanUpXdsConnection();
  }

  void initialize() override { initialize(true); }

  void initialize(bool failover_defined) {
    failover_defined_ = failover_defined;

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Configure the primary ADS gRPC.
      auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
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
      if (failover_defined_) {
        // Configure the API to use a failover gRPC service.
        auto* failover_grpc_service = ads_config->add_grpc_services();
        setGrpcService(*failover_grpc_service, "failover_ads_cluster",
                       failover_xds_upstream_->localAddress());
        auto* failover_ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
        failover_ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        failover_ads_cluster->set_name("failover_ads_cluster");
        envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext failover_context;
        auto* failover_validation_context =
            failover_context.mutable_common_tls_context()->mutable_validation_context();
        failover_validation_context->mutable_trusted_ca()->set_filename(
            TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
        auto* failover_san_matcher =
            failover_validation_context->add_match_typed_subject_alt_names();
        failover_san_matcher->mutable_matcher()->set_suffix("lyft.com");
        failover_san_matcher->set_san_type(
            envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
        if (clientType() == Grpc::ClientType::GoogleGrpc) {
          auto* failover_google_grpc = failover_grpc_service->mutable_google_grpc();
          auto* failover_ssl_creds =
              failover_google_grpc->mutable_channel_credentials()->mutable_ssl_credentials();
          failover_ssl_creds->mutable_root_certs()->set_filename(
              TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
        }
        failover_ads_cluster->mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
        failover_ads_cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(
            failover_context);
      }
    });
    HttpIntegrationTest::initialize();
    // Do not respond to the initial primary stream request.
  }

  void createFailoverXdsUpstream() {
    if (tls_xds_upstream_ == false) {
      addFakeUpstream(Http::CodecType::HTTP2);
    } else {
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
      auto* common_tls_context = tls_context.mutable_common_tls_context();
      common_tls_context->add_alpn_protocols(Http::Utility::AlpnNames::get().Http2);
      auto* tls_cert = common_tls_context->add_tls_certificates();
      tls_cert->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
      tls_cert->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
      auto cfg = *Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
          tls_context, factory_context_);
      // upstream_stats_store_ should have been initialized be prior call to
      // BaseIntegrationTest::createXdsUpstream().
      ASSERT(upstream_stats_store_ != nullptr);
      auto context = *Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
          std::move(cfg), context_manager_, *upstream_stats_store_->rootScope(),
          std::vector<std::string>{});
      addFakeUpstream(std::move(context), Http::CodecType::HTTP2, /*autonomous_upstream=*/false);
    }
    failover_xds_upstream_ = fake_upstreams_.back().get();
  }

  void initializeFailoverXdsStream() {
    if (failover_xds_stream_ == nullptr) {
      auto result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
      RELEASE_ASSERT(result, result.message());
      failover_xds_stream_->startGrpcStream();
    }
  }

  void createXdsUpstream() override {
    BaseIntegrationTest::createXdsUpstream();
    // Setup the failover xDS upstream.
    createFailoverXdsUpstream();
  }

  void cleanUpConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  // Waits for a primary source connected and immediately disconnects.
  void primaryConnectionFailure() {
    AssertionResult result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->close();
    RELEASE_ASSERT(result, result.message());
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name) {
    return ConfigHelper::buildClusterLoadAssignment(
        name, Network::Test::getLoopbackAddressString(ipVersion()),
        fake_upstreams_[0]->localAddress()->ip()->port());
  }

  void makeSingleRequest() {
    registerTestServerPorts({"http"});
    testRouterHeaderOnlyRequestAndResponse();
    cleanupUpstreamAndDownstream();
  }

  envoy::config::listener::v3::Listener buildSimpleListener(const std::string& listener_name,
                                                            const std::string& cluster_name) {
    std::string hcm = fmt::format(
        R"EOF(
          filters:
          - name: http
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              stat_prefix: {}
              codec_type: HTTP2
              route_config:
                name: route_config_1
                virtual_hosts:
                - name: integration
                  domains: ["*"]
                  routes:
                  - match: {{ prefix: "/" }}
                    route: {{ cluster: "{}" }}
              http_filters:
              - name: envoy.filters.http.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      )EOF",
        "ads_test", cluster_name);
    return ConfigHelper::buildBaseListener(
        listener_name, Network::Test::getLoopbackAddressString(ipVersion()), hcm);
  }

  void validateAllXdsResponsesAndDataplaneRequest(FakeStream* xds_stream) {
    EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "", {}, {}, {}, true,
                                        Grpc::Status::WellKnownGrpcStatus::Ok, "", xds_stream));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        CdsTypeUrl, {ConfigHelper::buildCluster("cluster_0")},
        {ConfigHelper::buildCluster("cluster_0")}, {}, "1", {}, xds_stream);
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
    test_server_->waitForGaugeEq("cluster.cluster_0.warming_state", 1);
    EXPECT_TRUE(compareDiscoveryRequest(EdsTypeUrl, "", {"cluster_0"}, {"cluster_0"}, {}, false,
                                        Grpc::Status::WellKnownGrpcStatus::Ok, "", xds_stream));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        EdsTypeUrl, {buildClusterLoadAssignment("cluster_0")},
        {buildClusterLoadAssignment("cluster_0")}, {}, "1", {}, xds_stream);

    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
    test_server_->waitForGaugeEq("cluster.cluster_0.warming_state", 0);

    EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "1", {}, {}, {}, false,
                                        Grpc::Status::WellKnownGrpcStatus::Ok, "", xds_stream));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}, false,
                                        Grpc::Status::WellKnownGrpcStatus::Ok, "", xds_stream));

    sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
        LdsTypeUrl, {buildSimpleListener("listener_0", "cluster_0")},
        {buildSimpleListener("listener_0", "cluster_0")}, {}, "1", {}, xds_stream);

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                        {"cluster_0"}, {}, {}, false,
                                        Grpc::Status::WellKnownGrpcStatus::Ok, "", xds_stream));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}, {}, {}, false,
                                        Grpc::Status::WellKnownGrpcStatus::Ok, "", xds_stream));

    test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
    makeSingleRequest();
  }

  bool failover_defined_;
  // Holds the failover xDS server data (if needed).
  FakeUpstream* failover_xds_upstream_;
  FakeHttpConnectionPtr failover_xds_connection_;
  FakeStreamPtr failover_xds_stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeltaWildcard, XdsFailoverAdsIntegrationTest,
                         DELTA_SOTW_UNIFIED_GRPC_CLIENT_INTEGRATION_PARAMS);

// Validate that when there's no failover defined (but runtime flag enabled),
// the primary is used.
TEST_P(XdsFailoverAdsIntegrationTest, NoFailoverBasic) {
  initialize(false);

  createXdsConnection();
  AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  xds_stream_->startGrpcStream();

  // Ensure basic flow using the primary (when failover is not defined) works.
  validateAllXdsResponsesAndDataplaneRequest(xds_stream_.get());
}

// Validate that when there's failover defined and the primary is available,
// then a connection to the failover won't be attempted.
TEST_P(XdsFailoverAdsIntegrationTest, FailoverNotAttemptedWhenPrimaryAvailable) {
  // This test does not work when GoogleGrpc is used, because GoogleGrpc
  // attempts to create the connection when the client is created (not when it
  // actually attempts to send a request). This is due to the call to GetChannel:
  // https://github.com/envoyproxy/envoy/blob/c9848c7be992c489a16a3218ec4bf373c9616d70/source/common/grpc/google_async_client_impl.cc#L106
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::GoogleGrpc);
  initialize();

  createXdsConnection();
  AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  xds_stream_->startGrpcStream();

  // Ensure basic flow using the primary (while failover is defined) works.
  validateAllXdsResponsesAndDataplaneRequest(xds_stream_.get());

  // A failover connection should not be attempted, so a failure is expected here.
  // Setting smaller timeout to avoid long execution times.
  EXPECT_FALSE(failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_,
                                                             std::chrono::seconds(5)));
}

// Validates that when there's a failover defined, and the primary isn't responding,
// then Envoy will use the failover, and will receive a valid config.
TEST_P(XdsFailoverAdsIntegrationTest, StartupPrimaryNotResponding) {
  initialize();

  // Expect a connection to the primary. Reject the connection immediately.
  primaryConnectionFailure();

  // Expect another connection attempt to the primary. Reject the stream (gRPC failure) immediately.
  // As this is a 2nd consecutive failure, it will trigger failover.
  primaryConnectionFailure();

  AssertionResult result =
      failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
  RELEASE_ASSERT(result, result.message());
  // Failover is healthy, start the ADS gRPC stream.
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  RELEASE_ASSERT(result, result.message());
  failover_xds_stream_->startGrpcStream();

  // Ensure basic flow using the failover source works.
  validateAllXdsResponsesAndDataplaneRequest(failover_xds_stream_.get());
}

// Validates that when there's a failover defined, and the primary returns a
// gRPC failure, then Envoy will use the failover, and will receive a valid config.
TEST_P(XdsFailoverAdsIntegrationTest, StartupPrimaryGrpcFailure) {
#ifdef ENVOY_ENABLE_UHV
  // With UHV the finishGrpcStream() isn't detected as invalid frame because of
  // no ":status" header, unless "envoy.reloadable_features.enable_universal_header_validator"
  // is also enabled.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  initialize();

  // Expect a connection to the primary. Send a gRPC failure immediately.
  AssertionResult result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Second attempt to the primary.
  // When using GoogleGrpc the same connection is reused, whereas for EnvoyGrpc
  // a new connection will be established.
  if (clientType() == Grpc::ClientType::EnvoyGrpc) {
    result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
  }
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  ASSERT(failover_xds_connection_ == nullptr);
  result = failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
  RELEASE_ASSERT(result, result.message());
  // Failover is healthy, start the ADS gRPC stream.
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  RELEASE_ASSERT(result, result.message());
  failover_xds_stream_->startGrpcStream();

  // Ensure basic flow using the failover source works.
  validateAllXdsResponsesAndDataplaneRequest(failover_xds_stream_.get());
}

// Validates that when there's a failover defined, and the primary returns a
// gRPC failure after sending headers, then Envoy will use the failover, and will receive a valid
// config.
TEST_P(XdsFailoverAdsIntegrationTest, StartupPrimaryGrpcFailureAfterHeaders) {
#ifdef ENVOY_ENABLE_UHV
  // With UHV the finishGrpcStream() isn't detected as invalid frame because of
  // no ":status" header, unless "envoy.reloadable_features.enable_universal_header_validator"
  // is also enabled.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  initialize();

  // Expect a connection to the primary. Send a gRPC failure immediately.
  AssertionResult result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->startGrpcStream();
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Second attempt to the primary, reusing stream as headers were previously
  // sent.
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->startGrpcStream();
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  ASSERT(failover_xds_connection_ == nullptr);
  result = failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
  RELEASE_ASSERT(result, result.message());
  // Failover is healthy, start the ADS gRPC stream.
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  RELEASE_ASSERT(result, result.message());
  failover_xds_stream_->startGrpcStream();

  // Ensure basic flow using the failover source works.
  validateAllXdsResponsesAndDataplaneRequest(failover_xds_stream_.get());
}

// Validate that once primary answers, failover will not be used, even after disconnecting.
TEST_P(XdsFailoverAdsIntegrationTest, NoFailoverUseAfterPrimaryResponse) {
#ifdef ENVOY_ENABLE_UHV
  // With UHV the finishGrpcStream() isn't detected as invalid frame because of
  // no ":status" header, unless "envoy.reloadable_features.enable_universal_header_validator"
  // is also enabled.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  initialize();

  // Let the primary source respond.
  createXdsConnection();
  AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  xds_stream_->startGrpcStream();

  // Ensure basic flow with failover works.
  EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "", {}, {}, {}, true,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      CdsTypeUrl, {ConfigHelper::buildCluster("cluster_0")},
      {ConfigHelper::buildCluster("cluster_0")}, {}, "1", {}, xds_stream_.get());
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.warming_state", 1);

  // Envoy has received a CDS response, it means the primary is available.
  // Now disconnect the primary.
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // In this case (received a response), both EnvoyGrpc and GoogleGrpc keep the connection open.
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  // Immediately fail the connection.
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Ensure that Envoy still attempts to connect to the primary,
  // and keep disconnecting a few times and validate that the failover
  // connection isn't attempted.
  for (int i = 3; i < 5; ++i) {
    // EnvoyGrpc will disconnect if the gRPC stream is immediately closed (as
    // done above).
    if (clientType() == Grpc::ClientType::EnvoyGrpc) {
      result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
      RELEASE_ASSERT(result, result.message());
    }
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    // Immediately fail the connection.
    xds_stream_->finishGrpcStream(Grpc::Status::Internal);
  }

  // When GoogleGrpc is used, a connection to the failover_xds_upstream will be
  // attempted, but no stream will be created. When EnvoyGrpc is used, no
  // connection to the failover will be attempted.
  if (clientType() == Grpc::ClientType::EnvoyGrpc) {
    // A failover connection should not be attempted, so a failure is expected here.
    // Setting smaller timeout to avoid long execution times.
    EXPECT_FALSE(failover_xds_upstream_->waitForHttpConnection(
        *dispatcher_, failover_xds_connection_, std::chrono::seconds(1)));
  } else {
    result = failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
    RELEASE_ASSERT(result, result.message());
    EXPECT_FALSE(failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_,
                                                            std::chrono::seconds(1)));
  }

  // Allow a connection to the primary.
  // Expect a connection to the primary when using EnvoyGrpc.
  // In case GoogleGrpc is used the current connection will be reused (new stream).
  if (clientType() == Grpc::ClientType::EnvoyGrpc) {
    result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
  }
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  xds_stream_->startGrpcStream();

  // The rest will be a normal primary source xDS back and forth.
  validateAllXdsResponsesAndDataplaneRequest(xds_stream_.get());
}

// Validate that once failover answers, primary will not be used, even after disconnecting.
TEST_P(XdsFailoverAdsIntegrationTest, NoPrimaryUseAfterFailoverResponse) {
#ifdef ENVOY_ENABLE_UHV
  // With UHV the finishGrpcStream() isn't detected as invalid frame because of
  // no ":status" header, unless "envoy.reloadable_features.enable_universal_header_validator"
  // is also enabled.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  initialize();

  // 2 consecutive primary failures.
  // Expect a connection to the primary. Reject the connection immediately.
  primaryConnectionFailure();
  // Expect another connection attempt to the primary. Reject the stream (gRPC failure) immediately.
  // As this is a 2nd consecutive failure, it will trigger failover.
  primaryConnectionFailure();

  AssertionResult result =
      failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
  RELEASE_ASSERT(result, result.message());
  // Failover is healthy, start the ADS gRPC stream.
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  RELEASE_ASSERT(result, result.message());
  failover_xds_stream_->startGrpcStream();

  // Ensure basic flow with failover works.
  EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "", {}, {}, {}, true,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      failover_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      CdsTypeUrl, {ConfigHelper::buildCluster("cluster_0")},
      {ConfigHelper::buildCluster("cluster_0")}, {}, "1", {}, failover_xds_stream_.get());
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.warming_state", 1);

  // Envoy has received a CDS response, it means the primary is available.
  // Now disconnect the primary.
  failover_xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // In this case (received a response), both EnvoyGrpc and GoogleGrpc keep the connection open.
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  RELEASE_ASSERT(result, result.message());
  // Immediately fail the connection.
  failover_xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Ensure that Envoy still attempts to connect to the primary,
  // and keep disconnecting a few times and validate that the failover
  // connection isn't attempted.
  for (int i = 3; i < 5; ++i) {
    // EnvoyGrpc will disconnect if the gRPC stream is immediately closed (as
    // done above).
    if (clientType() == Grpc::ClientType::EnvoyGrpc) {
      result =
          failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
      RELEASE_ASSERT(result, result.message());
    }
    result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
    RELEASE_ASSERT(result, result.message());
    // Immediately fail the connection.
    failover_xds_stream_->finishGrpcStream(Grpc::Status::Internal);
  }

  // When GoogleGrpc is used, a connection to the (primary) xds_upstream will be
  // attempted, but no stream will be created. When EnvoyGrpc is used, no
  // connection to the primary will be attempted.
  if (clientType() == Grpc::ClientType::EnvoyGrpc) {
    // A primary connection should not be attempted, so a failure is expected here.
    // Setting smaller timeout to avoid long execution times.
    EXPECT_FALSE(xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_,
                                                      std::chrono::seconds(1)));
  } else {
    result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    EXPECT_FALSE(
        xds_connection_->waitForNewStream(*dispatcher_, xds_stream_, std::chrono::seconds(1)));
  }

  // Allow a connection to the failover.
  // Expect a connection to the failover when using EnvoyGrpc.
  // In case GoogleGrpc is used the current connection will be reused (new stream).
  if (clientType() == Grpc::ClientType::EnvoyGrpc) {
    result = failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
    RELEASE_ASSERT(result, result.message());
  }
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  failover_xds_stream_->startGrpcStream();

  // The rest will be a normal failover source xDS back and forth.
  validateAllXdsResponsesAndDataplaneRequest(failover_xds_stream_.get());
}
} // namespace Envoy
