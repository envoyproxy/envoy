#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/common/logger.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"
#include "source/common/tls/ssl_socket.h"

#include "test/integration/ads_integration.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {

namespace {
const auto CdsTypeUrl = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
const auto EdsTypeUrl = Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>();
const auto LdsTypeUrl = Config::getTypeUrl<envoy::config::listener::v3::Listener>();
} // namespace

// Tests the use of Envoy with a primary and failover sources.
class XdsFailoverAdsIntegrationTest : public AdsDeltaSotwIntegrationSubStateParamTest,
                                      public Event::TestUsingSimulatedTime,
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
      if (clientType() == Grpc::ClientType::EnvoyGrpc) {
        // Set smaller retry limits to make it the time to wait
        // more predictable.
        auto* envoy_grpc = grpc_service->mutable_envoy_grpc();
        envoy_grpc->mutable_retry_policy()
            ->mutable_retry_back_off()
            ->mutable_base_interval()
            ->set_nanos(900 * 1e6);
        envoy_grpc->mutable_retry_policy()
            ->mutable_retry_back_off()
            ->mutable_max_interval()
            ->set_seconds(1);
      } else {
        // clientType() == Grpc::ClientType::GoogleGrpc.
        auto* google_grpc = grpc_service->mutable_google_grpc();
        auto* ssl_creds = google_grpc->mutable_channel_credentials()->mutable_ssl_credentials();
        ssl_creds->mutable_root_certs()->set_filename(
            TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
      }
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
          tls_context, factory_context_, false);
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

  // Waits for a failover source connected and immediately disconnects.
  void failoverConnectionFailure() {
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

  void waitForPrimaryXdsRetryTimer(uint32_t expected_failures = 1, uint32_t seconds = 1) {
    // When a gRPC stream is closed, first the onEstablishmentFailure() callbacks are
    // called and retry timer is added after. To make the test (current) thread
    // increase the simulated time *after* the retry timer is added is done as
    // follows:
    // 1. The test thread waits for the CDS update_failure counter update which
    // will occur as part of the onEstablishmentFailure() callbacks.
    // 2. The test thread will install a barrier notification in Envoy's main
    // thread dispatcher. This will be called after the main thread finishes the
    // current invocation of gRPC stream closure piece of code that will also
    // enable the retry timer.
    // 3. The test thread will increase the simulated time.
    test_server_->waitForCounterGe("cluster_manager.cds.update_failure", expected_failures);
    absl::Notification notification;
    test_server_->server().dispatcher().post([&]() { notification.Notify(); });
    notification.WaitForNotification();
    timeSystem().advanceTimeWait(std::chrono::seconds(seconds));
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

  EXPECT_EQ(1, test_server_->gauge("control_plane.connected_state")->value());
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
  ASSERT_TRUE(xds_connection_->waitForDisconnect());

  // The CDS request fails when the primary disconnects. After that fetch the config
  // dump to ensure that the retry timer kicks in.
  waitForPrimaryXdsRetryTimer();

  // Expect another connection attempt to the primary. Reject the stream (gRPC failure) immediately.
  // As this is a 2nd consecutive failure, it will trigger failover.
  primaryConnectionFailure();
  ASSERT_TRUE(xds_connection_->waitForDisconnect());

  // The CDS request fails when the primary disconnects.
  test_server_->waitForCounterGe("cluster_manager.cds.update_failure", 2);

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

  // When EnvoyGrpc is used, the connection will be terminated.
  // When GoogleGrpc is used, only the stream will be terminated.
  if (clientType() == Grpc::ClientType::EnvoyGrpc) {
    ASSERT_TRUE(xds_connection_->waitForDisconnect());
  }

  // The CDS request fails when the primary disconnects. After that fetch the config
  // dump to ensure that the retry timer kicks in.
  waitForPrimaryXdsRetryTimer();

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

  // When EnvoyGrpc is used, the connection will be terminated.
  if (clientType() == Grpc::ClientType::EnvoyGrpc) {
    ASSERT_TRUE(xds_connection_->waitForDisconnect());
  }

  // The CDS request fails when the primary disconnects.
  test_server_->waitForCounterGe("cluster_manager.cds.update_failure", 2);

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

  // The CDS request fails when the primary disconnects. After that fetch the config
  // dump to ensure that the retry timer kicks in.
  waitForPrimaryXdsRetryTimer();

  // Second attempt to the primary, reusing stream as headers were previously
  // sent.
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->startGrpcStream();
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // The CDS request fails when the primary disconnects.
  test_server_->waitForCounterGe("cluster_manager.cds.update_failure", 2);

  ASSERT(failover_xds_connection_ == nullptr);
  result = failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
  RELEASE_ASSERT(result, result.message());
  // Failover is healthy, start the ADS gRPC stream.
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  RELEASE_ASSERT(result, result.message());
  failover_xds_stream_->startGrpcStream();

  // Ensure basic flow using the failover source works.
  validateAllXdsResponsesAndDataplaneRequest(failover_xds_stream_.get());

  EXPECT_EQ(2, test_server_->gauge("control_plane.connected_state")->value());
}

// Validate that once primary answers, failover will not be used, even after disconnecting.
TEST_P(XdsFailoverAdsIntegrationTest, NoFailoverUseAfterPrimaryResponse) {
  // These tests are not executed with GoogleGrpc because they are flaky due to
  // the large timeout values for retries.
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::GoogleGrpc);
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

  // CDS was successful, but EDS will fail. After that add a notification to the
  // main thread to ensure that the retry timer kicks in.
  test_server_->waitForCounterGe("cluster.cluster_0.update_failure", 1);
  absl::Notification notification;
  test_server_->server().dispatcher().post([&]() { notification.Notify(); });
  notification.WaitForNotification();
  timeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  // In this case (received a response), both EnvoyGrpc and GoogleGrpc keep the connection open.
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  // Immediately fail the connection.
  xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Ensure that Envoy still attempts to connect to the primary,
  // and keep disconnecting a few times and validate that the failover
  // connection isn't attempted.
  for (int i = 1; i < 5; ++i) {
    // In EnvoyGrpc, the CDS request fails when the primary disconnects. After that
    // fetch the config dump to ensure that the retry timer kicks in.
    ASSERT_TRUE(xds_connection_->waitForDisconnect());
    waitForPrimaryXdsRetryTimer(i);

    // EnvoyGrpc will disconnect if the gRPC stream is immediately closed (as
    // done above).
    result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    // Immediately fail the connection.
    xds_stream_->finishGrpcStream(Grpc::Status::Internal);
  }

  // When EnvoyGrpc is used, no new connection to the failover will be attempted.
  EXPECT_FALSE(failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_,
                                                             std::chrono::seconds(1)));

  // The CDS request fails when the primary disconnects. After that fetch the config
  // dump to ensure that the retry timer kicks in.
  ASSERT_TRUE(xds_connection_->waitForDisconnect());
  waitForPrimaryXdsRetryTimer(5);

  // Allow a connection to the primary.
  // Expect a connection to the primary when using EnvoyGrpc.
  // In case GoogleGrpc is used the current connection will be reused (new stream).
  result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  xds_stream_->startGrpcStream();

  // Validate that just the initial requests are sent to the primary.
  EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "1", {}, {}, {}, true,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      xds_stream_.get()));
  EXPECT_TRUE(compareDiscoveryRequest(EdsTypeUrl, "", {"cluster_0"}, {"cluster_0"}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      xds_stream_.get()));

  EXPECT_EQ(1, test_server_->gauge("control_plane.connected_state")->value());
}

// Validate that once failover responds, and then disconnects, primary will be attempted.
TEST_P(XdsFailoverAdsIntegrationTest, PrimaryUseAfterFailoverResponseAndDisconnect) {
  // These tests are not executed with GoogleGrpc because they are flaky due to
  // the large timeout values for retries.
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::GoogleGrpc);
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
  ASSERT_TRUE(xds_connection_->waitForDisconnect());
  // The CDS request fails when the primary disconnects. After that fetch the config
  // dump to ensure that the retry timer kicks in.
  // Expect another connection attempt to the primary. Reject the stream (gRPC failure) immediately.
  // As this is a 2nd consecutive failure, it will trigger failover.
  waitForPrimaryXdsRetryTimer();
  primaryConnectionFailure();
  ASSERT_TRUE(xds_connection_->waitForDisconnect());

  // The CDS request fails when the primary disconnects.
  test_server_->waitForCounterGe("cluster_manager.cds.update_failure", 2);

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
      CdsTypeUrl, {ConfigHelper::buildCluster("failover_cluster_0")},
      {ConfigHelper::buildCluster("failover_cluster_0")}, {}, "failover1", {},
      failover_xds_stream_.get());
  // Wait for an EDS request, and send its response.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  test_server_->waitForGaugeEq("cluster.failover_cluster_0.warming_state", 1);
  EXPECT_TRUE(compareDiscoveryRequest(
      EdsTypeUrl, "", {"failover_cluster_0"}, {"failover_cluster_0"}, {}, false,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", failover_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      EdsTypeUrl, {buildClusterLoadAssignment("failover_cluster_0")},
      {buildClusterLoadAssignment("failover_cluster_0")}, {}, "failover1", {},
      failover_xds_stream_.get());
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
  test_server_->waitForGaugeEq("cluster.failover_cluster_0.warming_state", 0);
  EXPECT_EQ(2, test_server_->gauge("control_plane.connected_state")->value());
  EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "failover1", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      failover_xds_stream_.get()));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      failover_xds_stream_.get()));

  // Envoy has received CDS and EDS responses, it means the failover is available.
  // Now disconnect the failover source.
  failover_xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // CDS and EDS were successful, but LDS will fail. After that add a notification to the
  // main thread to ensure that the retry timer kicks in.
  test_server_->waitForCounterGe("listener_manager.lds.update_failure", 1);
  absl::Notification notification;
  test_server_->server().dispatcher().post([&]() { notification.Notify(); });
  notification.WaitForNotification();
  timeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  // The failover disconnected, so the next step is trying to connect to the
  // primary. First ensure that the failover isn't being attempted, and then let
  // the connection to the primary succeed.
  EXPECT_FALSE(failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_,
                                                             std::chrono::seconds(5)));
  EXPECT_TRUE(xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_));

  // Allow receiving config from the primary.
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  xds_stream_->startGrpcStream();

  // Ensure basic flow with primary works. Validate that the
  // initial_resource_versions for delta-xDS is empty.
  const absl::flat_hash_map<std::string, std::string> empty_initial_resource_versions_map;
  EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "", {}, {}, {}, true,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "", xds_stream_.get(),
                                      OptRef(empty_initial_resource_versions_map)));
  EXPECT_TRUE(compareDiscoveryRequest(EdsTypeUrl, "", {"failover_cluster_0"},
                                      {"failover_cluster_0"}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "", xds_stream_.get(),
                                      OptRef(empty_initial_resource_versions_map)));
  EXPECT_TRUE(compareDiscoveryRequest(LdsTypeUrl, "", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "", xds_stream_.get(),
                                      OptRef(empty_initial_resource_versions_map)));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      CdsTypeUrl, {ConfigHelper::buildCluster("primary_cluster_0")},
      {ConfigHelper::buildCluster("primary_cluster_0")}, {}, "primary1", {}, xds_stream_.get());
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  test_server_->waitForGaugeEq("cluster.primary_cluster_0.warming_state", 1);

  // Expect an updated failover EDS request.
  EXPECT_TRUE(compareDiscoveryRequest(EdsTypeUrl, "", {"primary_cluster_0"}, {"primary_cluster_0"},
                                      {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      xds_stream_.get()));
  EXPECT_EQ(1, test_server_->gauge("control_plane.connected_state")->value());
}

// Validates that if failover is used, and then disconnected, and the primary
// still doesn't respond, failover will be attempted with the correct
// initial_resource_versions.
TEST_P(XdsFailoverAdsIntegrationTest, FailoverUseAfterFailoverResponseAndDisconnect) {
  // These tests are not executed with GoogleGrpc because they are flaky due to
  // the large timeout values for retries.
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::GoogleGrpc);
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
  ASSERT_TRUE(xds_connection_->waitForDisconnect());
  // The CDS request fails when the primary disconnects. After that fetch the config
  // dump to ensure that the retry timer kicks in.
  // Expect another connection attempt to the primary. Reject the stream (gRPC failure) immediately.
  // As this is a 2nd consecutive failure, it will trigger failover.
  waitForPrimaryXdsRetryTimer();
  primaryConnectionFailure();
  ASSERT_TRUE(xds_connection_->waitForDisconnect());

  // The CDS request fails when the primary disconnects.
  test_server_->waitForCounterGe("cluster_manager.cds.update_failure", 2);

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
      CdsTypeUrl, {ConfigHelper::buildCluster("failover_cluster_0")},
      {ConfigHelper::buildCluster("failover_cluster_0")}, {}, "failover1", {},
      failover_xds_stream_.get());
  // Wait for an EDS request, and send its response.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  test_server_->waitForGaugeEq("cluster.failover_cluster_0.warming_state", 1);
  EXPECT_TRUE(compareDiscoveryRequest(
      EdsTypeUrl, "", {"failover_cluster_0"}, {"failover_cluster_0"}, {}, false,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", failover_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      EdsTypeUrl, {buildClusterLoadAssignment("failover_cluster_0")},
      {buildClusterLoadAssignment("failover_cluster_0")}, {}, "failover1", {},
      failover_xds_stream_.get());
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
  test_server_->waitForGaugeEq("cluster.failover_cluster_0.warming_state", 0);
  EXPECT_EQ(2, test_server_->gauge("control_plane.connected_state")->value());
  EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "failover1", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      failover_xds_stream_.get()));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      failover_xds_stream_.get()));

  // Envoy has received CDS and EDS responses, it means the failover is available.
  // Now disconnect the failover.
  failover_xds_stream_->finishGrpcStream(Grpc::Status::Internal);
  EXPECT_TRUE(failover_xds_connection_->close());
  ASSERT_TRUE(failover_xds_connection_->waitForDisconnect());

  // Wait longer due to the fixed 5 seconds failover.
  waitForPrimaryXdsRetryTimer(3, 5);

  // The failover disconnected, so the next step is trying to connect to the
  // primary source. Disconnect the primary source immediately.
  primaryConnectionFailure();
  ASSERT_TRUE(xds_connection_->waitForDisconnect());

  // Expect a connection to the failover source to be attempted.
  result = failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
  RELEASE_ASSERT(result, result.message());

  // Failover is healthy, start the ADS gRPC stream.
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  RELEASE_ASSERT(result, result.message());
  failover_xds_stream_->startGrpcStream();

  // Ensure basic flow with failover after it was connected to failover is
  // preserved. The initial resource versions of LDS will be empty, because no
  // LDS response was previously sent.
  const absl::flat_hash_map<std::string, std::string> cds_eds_initial_resource_versions_map{
      {"failover_cluster_0", "failover1"}};
  const absl::flat_hash_map<std::string, std::string> empty_initial_resource_versions_map;
  EXPECT_TRUE(compareDiscoveryRequest(
      CdsTypeUrl, "", {}, {}, {}, true, Grpc::Status::WellKnownGrpcStatus::Ok, "",
      failover_xds_stream_.get(), OptRef(cds_eds_initial_resource_versions_map)));
  EXPECT_TRUE(compareDiscoveryRequest(
      EdsTypeUrl, "", {"failover_cluster_0"}, {"failover_cluster_0"}, {}, false,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", failover_xds_stream_.get(),
      OptRef(cds_eds_initial_resource_versions_map)));
  EXPECT_TRUE(compareDiscoveryRequest(
      LdsTypeUrl, "", {}, {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
      failover_xds_stream_.get(), OptRef(empty_initial_resource_versions_map)));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      CdsTypeUrl, {ConfigHelper::buildCluster("failover_cluster_1")},
      {ConfigHelper::buildCluster("failover_cluster_1")}, {}, "failover2", {},
      failover_xds_stream_.get());
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  test_server_->waitForGaugeEq("cluster.failover_cluster_1.warming_state", 1);

  // Expect an updated failover EDS request.
  EXPECT_TRUE(compareDiscoveryRequest(
      EdsTypeUrl, "", {"failover_cluster_1"}, {"failover_cluster_1"}, {}, false,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", failover_xds_stream_.get()));
  EXPECT_EQ(2, test_server_->gauge("control_plane.connected_state")->value());
}

// Validate that once failover responds, and then disconnects, Envoy
// will alternate between the primary and failover sources (multiple times) if
// both are not responding.
TEST_P(XdsFailoverAdsIntegrationTest,
       PrimaryAndFailoverAttemptsAfterFailoverResponseAndDisconnect) {
  // These tests are not executed with GoogleGrpc because they are flaky due to
  // the large timeout values for retries.
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::GoogleGrpc);
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
  ASSERT_TRUE(xds_connection_->waitForDisconnect());
  // The CDS request fails when the primary disconnects. After that fetch the config
  // dump to ensure that the retry timer kicks in.
  // Expect another connection attempt to the primary. Reject the stream (gRPC failure) immediately.
  // As this is a 2nd consecutive failure, it will trigger failover.
  waitForPrimaryXdsRetryTimer();
  primaryConnectionFailure();
  ASSERT_TRUE(xds_connection_->waitForDisconnect());

  // The CDS request fails when the primary disconnects.
  test_server_->waitForCounterGe("cluster_manager.cds.update_failure", 2);

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
      CdsTypeUrl, {ConfigHelper::buildCluster("failover_cluster_0")},
      {ConfigHelper::buildCluster("failover_cluster_0")}, {}, "failover1", {},
      failover_xds_stream_.get());
  // Wait for an EDS request, and send its response.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  test_server_->waitForGaugeEq("cluster.failover_cluster_0.warming_state", 1);
  EXPECT_TRUE(compareDiscoveryRequest(
      EdsTypeUrl, "", {"failover_cluster_0"}, {"failover_cluster_0"}, {}, false,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", failover_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      EdsTypeUrl, {buildClusterLoadAssignment("failover_cluster_0")},
      {buildClusterLoadAssignment("failover_cluster_0")}, {}, "failover1", {},
      failover_xds_stream_.get());
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
  test_server_->waitForGaugeEq("cluster.failover_cluster_0.warming_state", 0);
  EXPECT_EQ(2, test_server_->gauge("control_plane.connected_state")->value());
  EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "failover1", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      failover_xds_stream_.get()));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      failover_xds_stream_.get()));

  // Envoy has received CDS and EDS responses, it means the failover is available.
  // Now disconnect the failover source.
  failover_xds_stream_->finishGrpcStream(Grpc::Status::Internal);
  EXPECT_TRUE(failover_xds_connection_->close());
  ASSERT_TRUE(failover_xds_connection_->waitForDisconnect());

  // Ensure Envoy alternates between primary and failover.
  // Up to this step there were 3 CDS update failures. In each iteration of the
  // next loop there are going to be 2 failures (one for primary and another for
  // failover).
  for (int i = 1; i < 3; ++i) {
    // Wait longer due to the fixed 5 seconds failover .
    waitForPrimaryXdsRetryTimer(2 * i + 1, 5);

    // The failover disconnected, so the next step is trying to connect to the
    // primary source. Disconnect the primary source immediately.
    primaryConnectionFailure();
    ASSERT_TRUE(xds_connection_->waitForDisconnect());

    // Expect a connection to the failover source to be attempted. Disconnect
    // immediately.
    result = failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = failover_xds_connection_->close();
    RELEASE_ASSERT(result, result.message());
    ASSERT_TRUE(failover_xds_connection_->waitForDisconnect());
  }
}

// Validation that when envoy.reloadable_features.xds_failover_to_primary_enabled is disabled
// and after the failover responds and then disconnected, Envoy will only
// try to reconnect to the failover.
// This test will be removed once envoy.reloadable_features.xds_failover_to_primary_enabled
// is deprecated.
TEST_P(XdsFailoverAdsIntegrationTest, NoPrimaryUseAfterFailoverResponse) {
  // These tests are not executed with GoogleGrpc because they are flaky due to
  // the large timeout values for retries.
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::GoogleGrpc);
#ifdef ENVOY_ENABLE_UHV
  // With UHV the finishGrpcStream() isn't detected as invalid frame because of
  // no ":status" header, unless "envoy.reloadable_features.enable_universal_header_validator"
  // is also enabled.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  config_helper_.addRuntimeOverride("envoy.reloadable_features.xds_failover_to_primary_enabled",
                                    "false");
  // Set a long LDS initial_fetch_timeout to prevent test flakiness when
  // reconnecting to the failover multiple times.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* lds_config = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
    lds_config->mutable_initial_fetch_timeout()->set_seconds(100);
  });
  initialize();

  // 2 consecutive primary failures.
  // Expect a connection to the primary. Reject the connection immediately.
  primaryConnectionFailure();
  ASSERT_TRUE(xds_connection_->waitForDisconnect());
  // The CDS request fails when the primary disconnects. After that fetch the config
  // dump to ensure that the retry timer kicks in.
  // Expect another connection attempt to the primary. Reject the stream (gRPC failure) immediately.
  // As this is a 2nd consecutive failure, it will trigger failover.
  waitForPrimaryXdsRetryTimer();
  primaryConnectionFailure();
  ASSERT_TRUE(xds_connection_->waitForDisconnect());

  // The CDS request fails when the primary disconnects.
  test_server_->waitForCounterGe("cluster_manager.cds.update_failure", 2);

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
      CdsTypeUrl, {ConfigHelper::buildCluster("failover_cluster_0")},
      {ConfigHelper::buildCluster("failover_cluster_0")}, {}, "failover1", {},
      failover_xds_stream_.get());
  // Wait for an EDS request, and send its response.
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 1);
  test_server_->waitForGaugeEq("cluster.failover_cluster_0.warming_state", 1);
  // Ensure basic flow with failover works.
  EXPECT_TRUE(compareDiscoveryRequest(
      EdsTypeUrl, "", {"failover_cluster_0"}, {"failover_cluster_0"}, {}, false,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", failover_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      EdsTypeUrl, {buildClusterLoadAssignment("failover_cluster_0")},
      {buildClusterLoadAssignment("failover_cluster_0")}, {}, "failover1", {},
      failover_xds_stream_.get());
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 2);
  test_server_->waitForGaugeEq("cluster.failover_cluster_0.warming_state", 0);
  EXPECT_EQ(2, test_server_->gauge("control_plane.connected_state")->value());
  EXPECT_TRUE(compareDiscoveryRequest(CdsTypeUrl, "failover1", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      failover_xds_stream_.get()));
  EXPECT_TRUE(compareDiscoveryRequest(LdsTypeUrl, "", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      failover_xds_stream_.get()));

  // Envoy has received CDS and EDS responses, it means the failover is available.
  // Now disconnect the failover source, this should result in an LDS failure.
  // After that add a notification to the main thread to ensure that the retry timer kicks in.
  failover_xds_stream_->finishGrpcStream(Grpc::Status::Internal);
  test_server_->waitForCounterGe("listener_manager.lds.update_failure", 1);
  absl::Notification notification;
  test_server_->server().dispatcher().post([&]() { notification.Notify(); });
  notification.WaitForNotification();
  timeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  // In this case (received a response), both EnvoyGrpc and GoogleGrpc keep the connection open.
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  RELEASE_ASSERT(result, result.message());
  // Immediately fail the connection.
  failover_xds_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Ensure that Envoy still attempts to connect to the failover,
  // and keep disconnecting a few times and validate that the primary
  // connection isn't attempted.
  for (int i = 1; i < 5; ++i) {
    ASSERT_TRUE(failover_xds_connection_->waitForDisconnect());
    // Wait longer due to the fixed 5 seconds failover .
    waitForPrimaryXdsRetryTimer(i, 6);
    // EnvoyGrpc will disconnect if the gRPC stream is immediately closed (as
    // done above).
    result = failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
    RELEASE_ASSERT(result, result.message());
    // Immediately fail the connection.
    failover_xds_stream_->finishGrpcStream(Grpc::Status::Internal);
  }

  // When EnvoyGrpc is used, no new connection to the primary will be attempted.
  EXPECT_FALSE(
      xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_, std::chrono::seconds(1)));

  ASSERT_TRUE(failover_xds_connection_->waitForDisconnect());
  // Wait longer due to the fixed 5 seconds failover .
  waitForPrimaryXdsRetryTimer(5, 6);

  // Allow a connection to the failover.
  // Expect a connection to the failover when using EnvoyGrpc.
  // In case GoogleGrpc is used the current connection will be reused (new stream).
  result = failover_xds_upstream_->waitForHttpConnection(*dispatcher_, failover_xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = failover_xds_connection_->waitForNewStream(*dispatcher_, failover_xds_stream_);
  failover_xds_stream_->startGrpcStream();

  // Validate that the initial requests with known versions are sent to the
  // failover source.
  const absl::flat_hash_map<std::string, std::string> cds_eds_initial_resource_versions_map{
      {"failover_cluster_0", "failover1"}};
  const absl::flat_hash_map<std::string, std::string> empty_initial_resource_versions_map;
  EXPECT_TRUE(compareDiscoveryRequest(
      CdsTypeUrl, "1", {}, {}, {}, true, Grpc::Status::WellKnownGrpcStatus::Ok, "",
      failover_xds_stream_.get(), OptRef(cds_eds_initial_resource_versions_map)));
  EXPECT_TRUE(compareDiscoveryRequest(
      EdsTypeUrl, "1", {"failover_cluster_0"}, {"failover_cluster_0"}, {}, false,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", failover_xds_stream_.get(),
      OptRef(cds_eds_initial_resource_versions_map)));
  EXPECT_TRUE(compareDiscoveryRequest(
      LdsTypeUrl, "", {}, {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
      failover_xds_stream_.get(), OptRef(empty_initial_resource_versions_map)));
}
} // namespace Envoy
