#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/service/health/v3/hds.pb.h"
#include "envoy/type/v3/http.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/config/metadata.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/health_discovery_service.h"

#include "test/common/upstream/utility.h"
#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

// TODO(jmarantz): switch this to simulated-time after debugging flakes.
class HdsIntegrationTest : public Grpc::VersionedGrpcClientIntegrationParamTest,
                           public HttpIntegrationTest {
public:
  HdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    hds_upstream_ = fake_upstreams_.back().get();
    HttpIntegrationTest::createUpstreams();
  }
  void initialize() override {
    setUpstreamCount(upstream_endpoints_);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Setup hds and corresponding gRPC cluster.
      auto* hds_config = bootstrap.mutable_hds_config();
      hds_config->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      hds_config->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("hds_cluster");
      hds_config->set_transport_api_version(apiVersion());
      auto* hds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      hds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      hds_cluster->mutable_circuit_breakers()->Clear();
      hds_cluster->set_name("hds_cluster");
      hds_cluster->mutable_http2_protocol_options();
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster_0->clear_load_assignment();
    });

    HttpIntegrationTest::initialize();

    // Endpoint connections
    if (tls_hosts_) {
      host_upstream_ =
          createFakeUpstream(HttpIntegrationTest::createUpstreamTlsContext(), http_conn_type_);
      host2_upstream_ =
          createFakeUpstream(HttpIntegrationTest::createUpstreamTlsContext(), http_conn_type_);
    } else {
      host_upstream_ = createFakeUpstream(http_conn_type_);
      host2_upstream_ = createFakeUpstream(http_conn_type_);
    }
  }

  // Sets up a connection between Envoy and the management server.
  void waitForHdsStream() {
    AssertionResult result =
        hds_upstream_->waitForHttpConnection(*dispatcher_, hds_fake_connection_);
    RELEASE_ASSERT(result, result.message());
    result = hds_fake_connection_->waitForNewStream(*dispatcher_, hds_stream_);
    RELEASE_ASSERT(result, result.message());
  }

  // Envoy sends health check messages to the endpoints
  void healthcheckEndpoints(std::string cluster2 = "") {
    ASSERT_TRUE(host_upstream_->waitForHttpConnection(*dispatcher_, host_fake_connection_));
    ASSERT_TRUE(host_fake_connection_->waitForNewStream(*dispatcher_, host_stream_));
    ASSERT_TRUE(host_stream_->waitForEndStream(*dispatcher_));

    EXPECT_EQ(host_stream_->headers().getPathValue(), "/healthcheck");
    EXPECT_EQ(host_stream_->headers().getMethodValue(), "GET");
    EXPECT_EQ(host_stream_->headers().getHostValue(), "anna");

    if (!cluster2.empty()) {
      ASSERT_TRUE(host2_upstream_->waitForHttpConnection(*dispatcher_, host2_fake_connection_));
      ASSERT_TRUE(host2_fake_connection_->waitForNewStream(*dispatcher_, host2_stream_));
      ASSERT_TRUE(host2_stream_->waitForEndStream(*dispatcher_));

      EXPECT_EQ(host2_stream_->headers().getPathValue(), "/healthcheck");
      EXPECT_EQ(host2_stream_->headers().getMethodValue(), "GET");
      EXPECT_EQ(host2_stream_->headers().getHostValue(), cluster2);
    }
  }

  // Clean up the connection between Envoy and the management server
  void cleanupHdsConnection() {
    if (hds_fake_connection_ != nullptr) {
      AssertionResult result = hds_fake_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = hds_fake_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  // Clean up connections between Envoy and endpoints
  void cleanupHostConnections() {
    if (host_fake_connection_ != nullptr) {
      AssertionResult result = host_fake_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = host_fake_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
    if (host2_fake_connection_ != nullptr) {
      AssertionResult result = host2_fake_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = host2_fake_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  // Creates a basic HealthCheckSpecifier message containing one endpoint and
  // one HTTP health_check
  envoy::service::health::v3::HealthCheckSpecifier
  makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType codec_type, bool use_tls) {
    envoy::service::health::v3::HealthCheckSpecifier server_health_check_specifier_;
    server_health_check_specifier_.mutable_interval()->set_nanos(100000000); // 0.1 seconds

    auto* cluster_health_check = server_health_check_specifier_.add_cluster_health_checks();

    cluster_health_check->set_cluster_name("anna");
    Network::Utility::addressToProtobufAddress(
        *host_upstream_->localAddress(),
        *cluster_health_check->add_locality_endpoints()->add_endpoints()->mutable_address());
    cluster_health_check->mutable_locality_endpoints(0)->mutable_locality()->set_region(
        "middle_earth");
    cluster_health_check->mutable_locality_endpoints(0)->mutable_locality()->set_zone("shire");
    cluster_health_check->mutable_locality_endpoints(0)->mutable_locality()->set_sub_zone(
        "hobbiton");
    auto* health_check = cluster_health_check->add_health_checks();
    health_check->mutable_timeout()->set_seconds(MaxTimeout);
    health_check->mutable_interval()->set_seconds(MaxTimeout);
    health_check->mutable_unhealthy_threshold()->set_value(2);
    health_check->mutable_healthy_threshold()->set_value(2);
    health_check->mutable_grpc_health_check();
    auto* http_health_check = health_check->mutable_http_health_check();
    http_health_check->set_path("/healthcheck");
    http_health_check->set_codec_client_type(codec_type);
    if (use_tls) {
      // Map our transport socket matches with our matcher.
      const std::string criteria_yaml = absl::StrFormat(
          R"EOF(
transport_socket_match_criteria:
  good_match: "true"
)EOF");
      health_check->MergeFrom(
          TestUtility::parseYaml<envoy::config::core::v3::HealthCheck>(criteria_yaml));

      // Create the list of all possible matches.
      const std::string match_yaml = absl::StrFormat(
          R"EOF(
transport_socket_matches:
- name: "tls_socket"
  match:
    good_match: "true"
  transport_socket:
    name: tls
    typed_config:
      "@type": type.googleapis.com/envoy.api.v2.auth.UpstreamTlsContext
      common_tls_context:
        tls_certificates:
        - certificate_chain: { filename: "%s" }
          private_key: { filename: "%s" }
  )EOF",
          TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"),
          TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
      cluster_health_check->MergeFrom(
          TestUtility::parseYaml<envoy::service::health::v3::ClusterHealthCheck>(match_yaml));
    }
    return server_health_check_specifier_;
  }

  envoy::service::health::v3::ClusterHealthCheck createSecondCluster(std::string name) {
    // Add endpoint
    envoy::service::health::v3::ClusterHealthCheck health_check;

    health_check.set_cluster_name(name);
    Network::Utility::addressToProtobufAddress(
        *host2_upstream_->localAddress(),
        *health_check.add_locality_endpoints()->add_endpoints()->mutable_address());
    health_check.mutable_locality_endpoints(0)->mutable_locality()->set_region("kounopetra");
    health_check.mutable_locality_endpoints(0)->mutable_locality()->set_zone("emplisi");
    health_check.mutable_locality_endpoints(0)->mutable_locality()->set_sub_zone("paris");

    health_check.add_health_checks()->mutable_timeout()->set_seconds(MaxTimeout);
    health_check.mutable_health_checks(0)->mutable_interval()->set_seconds(MaxTimeout);
    health_check.mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(2);
    health_check.mutable_health_checks(0)->mutable_healthy_threshold()->set_value(2);
    health_check.mutable_health_checks(0)->mutable_grpc_health_check();
    health_check.mutable_health_checks(0)
        ->mutable_http_health_check()
        ->set_hidden_envoy_deprecated_use_http2(false);
    health_check.mutable_health_checks(0)->mutable_http_health_check()->set_path("/healthcheck");

    return health_check;
  }

  // Creates a basic HealthCheckSpecifier message containing one endpoint and
  // one TCP health_check
  envoy::service::health::v3::HealthCheckSpecifier makeTcpHealthCheckSpecifier() {
    envoy::service::health::v3::HealthCheckSpecifier server_health_check_specifier_;
    server_health_check_specifier_.mutable_interval()->set_nanos(100000000); // 0.1 seconds

    auto* health_check = server_health_check_specifier_.add_cluster_health_checks();

    health_check->set_cluster_name("anna");
    Network::Utility::addressToProtobufAddress(
        *host_upstream_->localAddress(),
        *health_check->add_locality_endpoints()->add_endpoints()->mutable_address());
    health_check->mutable_locality_endpoints(0)->mutable_locality()->set_region("middle_earth");
    health_check->mutable_locality_endpoints(0)->mutable_locality()->set_zone("eriador");
    health_check->mutable_locality_endpoints(0)->mutable_locality()->set_sub_zone("rivendell");

    health_check->add_health_checks()->mutable_timeout()->set_seconds(MaxTimeout);
    health_check->mutable_health_checks(0)->mutable_interval()->set_seconds(MaxTimeout);
    health_check->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(2);
    health_check->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(2);
    auto* tcp_health_check = health_check->mutable_health_checks(0)->mutable_tcp_health_check();
    tcp_health_check->mutable_send()->set_text("50696E67");
    tcp_health_check->add_receive()->set_text("506F6E67");

    return server_health_check_specifier_;
  }

  // Checks if Envoy reported the health status of an endpoint correctly
  bool checkEndpointHealthResponse(envoy::service::health::v3::EndpointHealth endpoint,
                                   envoy::config::core::v3::HealthStatus healthy,
                                   Network::Address::InstanceConstSharedPtr address) {

    if (healthy != endpoint.health_status()) {
      return false;
    }
    if (address->ip()->port() != endpoint.endpoint().address().socket_address().port_value()) {
      return false;
    }
    if (address->ip()->addressAsString() !=
        endpoint.endpoint().address().socket_address().address()) {
      return false;
    }
    return true;
  }

  // Checks if the cluster counters are correct
  void checkCounters(int requests, int responses, int successes, int failures) {
    EXPECT_EQ(requests, test_server_->counter("hds_delegate.requests")->value());
    EXPECT_LE(responses, test_server_->counter("hds_delegate.responses")->value());
    EXPECT_EQ(successes, test_server_->counter("cluster.anna.health_check.success")->value());
    EXPECT_EQ(failures, test_server_->counter("cluster.anna.health_check.failure")->value());
  }

  void waitForEndpointHealthResponse(envoy::config::core::v3::HealthStatus healthy) {
    ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, response_));
    while (!checkEndpointHealthResponse(response_.endpoint_health_response().endpoints_health(0),
                                        healthy, host_upstream_->localAddress())) {
      ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, response_));
      EXPECT_EQ("POST", hds_stream_->headers().getMethodValue());
      EXPECT_EQ(TestUtility::getVersionedMethodPath("envoy.service.{1}.{0}.HealthDiscoveryService",
                                                    "StreamHealthCheck", apiVersion(),
                                                    /*use_alpha=*/false, serviceNamespace()),
                hds_stream_->headers().getPathValue());
      EXPECT_EQ("application/grpc", hds_stream_->headers().getContentTypeValue());
    }
  }

  // check response has correct format and health response.
  bool checkClusterEndpointHealthResponse(envoy::config::core::v3::HealthStatus healthy,
                                          Network::Address::InstanceConstSharedPtr address,
                                          int cluster, int locality, int endpoint) {
    // Ensure that this grpc message is a health response.
    if (response_.has_endpoint_health_response()) {
      auto& health_response = response_.endpoint_health_response();

      // Ensure that this response has a cluster available at the index.
      if (health_response.cluster_endpoints_health_size() > cluster) {
        auto& cluster_response = health_response.cluster_endpoints_health(cluster);

        // Ensure that this response has a locality available at the index.
        if (cluster_response.locality_endpoints_health_size() > locality) {
          auto& locality_response = cluster_response.locality_endpoints_health(locality);

          // Ensure that this response has a endpoint available at the index.
          if (locality_response.endpoints_health_size() > endpoint) {
            auto& endpoint_response = locality_response.endpoints_health(endpoint);

            // Check to see if this endpoint has specified health status.
            return checkEndpointHealthResponse(endpoint_response, healthy, address);
          }
        }
      }
    }

    // Some field is missing, return false.
    return false;
  }

  // wait until our response has desired health status for desired endpoint.
  bool waitForClusterHealthResponse(envoy::config::core::v3::HealthStatus healthy,
                                    Network::Address::InstanceConstSharedPtr address, int cluster,
                                    int locality, int endpoint) {
    // Get some response.
    if (!hds_stream_->waitForGrpcMessage(*dispatcher_, response_)) {
      return false;
    }

    // Check endpoint health status by indices.
    while (!checkClusterEndpointHealthResponse(healthy, address, cluster, locality, endpoint)) {
      if (!hds_stream_->waitForGrpcMessage(*dispatcher_, response_)) {
        return false;
      }

      EXPECT_EQ("POST", hds_stream_->headers().getMethodValue());
      EXPECT_EQ(TestUtility::getVersionedMethodPath("envoy.service.{1}.{0}.HealthDiscoveryService",
                                                    "StreamHealthCheck", apiVersion(),
                                                    /*use_alpha=*/false, serviceNamespace()),
                hds_stream_->headers().getPathValue());
      EXPECT_EQ("application/grpc", hds_stream_->headers().getContentTypeValue());
    }

    return true;
  }

  const std::string serviceNamespace() const {
    switch (apiVersion()) {
    case envoy::config::core::v3::ApiVersion::AUTO:
      FALLTHRU;
    case envoy::config::core::v3::ApiVersion::V2:
      return "discovery";
    case envoy::config::core::v3::ApiVersion::V3:
      return "health";
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  static constexpr uint32_t upstream_endpoints_ = 0;

  FakeHttpConnectionPtr hds_fake_connection_;
  FakeStreamPtr hds_stream_;
  FakeUpstream* hds_upstream_{};
  uint32_t hds_requests_{};
  FakeUpstreamPtr host_upstream_{};
  FakeUpstreamPtr host2_upstream_{};
  FakeStreamPtr host_stream_;
  FakeStreamPtr host2_stream_;
  FakeHttpConnectionPtr host_fake_connection_;
  FakeHttpConnectionPtr host2_fake_connection_;
  FakeRawConnectionPtr host_fake_raw_connection_;
  FakeHttpConnection::Type http_conn_type_{FakeHttpConnection::Type::HTTP1};
  bool tls_hosts_{false};

  static constexpr int MaxTimeout = 100;
  envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse envoy_msg_;
  envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse response_;
  envoy::service::health::v3::HealthCheckSpecifier server_health_check_specifier_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, HdsIntegrationTest,
                         VERSIONED_GRPC_CLIENT_INTEGRATION_PARAMS);

// Tests Envoy HTTP health checking a single healthy endpoint and reporting that it is
// indeed healthy to the server.
TEST_P(HdsIntegrationTest, SingleEndpointHealthyHttp) {
  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));
  EXPECT_EQ(envoy_msg_.health_check_request().capability().health_check_protocols(0),
            envoy::service::health::v3::Capability::HTTP);

  // Server asks for health checking
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends a health check message to an endpoint
  healthcheckEndpoints();

  // Endpoint responds to the health check
  host_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  host_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  waitForEndpointHealthResponse(envoy::config::core::v3::HEALTHY);

  checkCounters(1, 2, 1, 0);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests Envoy HTTP health checking a single endpoint that times out and reporting
// that it is unhealthy to the server.
TEST_P(HdsIntegrationTest, SingleEndpointTimeoutHttp) {
  initialize();
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);

  server_health_check_specifier_.mutable_cluster_health_checks(0)
      ->mutable_health_checks(0)
      ->mutable_timeout()
      ->set_seconds(0);
  server_health_check_specifier_.mutable_cluster_health_checks(0)
      ->mutable_health_checks(0)
      ->mutable_timeout()
      ->set_nanos(100000000); // 0.1 seconds

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends a health check message to an endpoint
  ASSERT_TRUE(host_upstream_->waitForRawConnection(host_fake_raw_connection_));

  // Endpoint doesn't respond to the health check
  ASSERT_TRUE(host_fake_raw_connection_->waitForDisconnect());

  // Receive updates until the one we expect arrives
  waitForEndpointHealthResponse(envoy::config::core::v3::TIMEOUT);

  checkCounters(1, 2, 0, 1);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests Envoy HTTP health checking a single unhealthy endpoint and reporting that it is
// indeed unhealthy to the server.
TEST_P(HdsIntegrationTest, SingleEndpointUnhealthyHttp) {
  initialize();
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends a health check message to an endpoint
  healthcheckEndpoints();

  // Endpoint responds to the health check
  host_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "404"}}, false);
  host_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  waitForEndpointHealthResponse(envoy::config::core::v3::UNHEALTHY);

  checkCounters(1, 2, 0, 1);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests Envoy TCP health checking an endpoint that doesn't respond and reporting that it is
// unhealthy to the server.
TEST_P(HdsIntegrationTest, SingleEndpointTimeoutTcp) {
  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));
  EXPECT_EQ(envoy_msg_.health_check_request().capability().health_check_protocols(1),
            envoy::service::health::v3::Capability::TCP);

  // Server asks for health checking
  server_health_check_specifier_ = makeTcpHealthCheckSpecifier();
  server_health_check_specifier_.mutable_cluster_health_checks(0)
      ->mutable_health_checks(0)
      ->mutable_timeout()
      ->set_seconds(0);
  server_health_check_specifier_.mutable_cluster_health_checks(0)
      ->mutable_health_checks(0)
      ->mutable_timeout()
      ->set_nanos(100000000); // 0.1 seconds

  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoys asks the endpoint if it's healthy
  ASSERT_TRUE(host_upstream_->waitForRawConnection(host_fake_raw_connection_));

  // No response from the endpoint
  ASSERT_TRUE(host_fake_raw_connection_->waitForDisconnect());

  // Receive updates until the one we expect arrives
  waitForEndpointHealthResponse(envoy::config::core::v3::TIMEOUT);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests Envoy TCP health checking a single healthy endpoint and reporting that it is
// indeed healthy to the server.
TEST_P(HdsIntegrationTest, SingleEndpointHealthyTcp) {
  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  server_health_check_specifier_ = makeTcpHealthCheckSpecifier();
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy asks the endpoint if it's healthy
  ASSERT_TRUE(host_upstream_->waitForRawConnection(host_fake_raw_connection_));
  ASSERT_TRUE(
      host_fake_raw_connection_->waitForData(FakeRawConnection::waitForInexactMatch("Ping")));
  AssertionResult result = host_fake_raw_connection_->write("Pong");
  RELEASE_ASSERT(result, result.message());

  // Receive updates until the one we expect arrives
  waitForEndpointHealthResponse(envoy::config::core::v3::HEALTHY);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests Envoy TCP health checking a single unhealthy endpoint and reporting that it is
// indeed unhealthy to the server.
TEST_P(HdsIntegrationTest, SingleEndpointUnhealthyTcp) {
  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  server_health_check_specifier_ = makeTcpHealthCheckSpecifier();
  server_health_check_specifier_.mutable_cluster_health_checks(0)
      ->mutable_health_checks(0)
      ->mutable_timeout()
      ->set_seconds(2);
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy asks the endpoint if it's healthy
  ASSERT_TRUE(host_upstream_->waitForRawConnection(host_fake_raw_connection_));
  ASSERT_TRUE(
      host_fake_raw_connection_->waitForData(FakeRawConnection::waitForInexactMatch("Ping")));
  AssertionResult result = host_fake_raw_connection_->write("Voronoi");
  RELEASE_ASSERT(result, result.message());

  // Receive updates until the one we expect arrives
  waitForEndpointHealthResponse(envoy::config::core::v3::UNHEALTHY);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests that Envoy can HTTP health check two hosts that are in the same cluster, and
// the same locality and report back the correct health statuses.
TEST_P(HdsIntegrationTest, TwoEndpointsSameLocality) {
  initialize();

  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);
  Network::Utility::addressToProtobufAddress(
      *host2_upstream_->localAddress(),
      *server_health_check_specifier_.mutable_cluster_health_checks(0)
           ->mutable_locality_endpoints(0)
           ->add_endpoints()
           ->mutable_address());
  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  healthcheckEndpoints("anna");

  // Endpoints respond to the health check
  host_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "404"}}, false);
  host_stream_->encodeData(1024, true);
  host2_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  host2_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  ASSERT_TRUE(waitForClusterHealthResponse(envoy::config::core::v3::HEALTHY,
                                           host2_upstream_->localAddress(), 0, 0, 1));

  // Ensure we have at least one cluster before trying to read it.
  ASSERT_EQ(response_.endpoint_health_response().cluster_endpoints_health_size(), 1);

  // store cluster response info for easier reference.
  const auto& cluster_response = response_.endpoint_health_response().cluster_endpoints_health(0);

  // Check cluster has correct name and number of localities (1)
  EXPECT_EQ(cluster_response.cluster_name(), "anna");
  ASSERT_EQ(cluster_response.locality_endpoints_health_size(), 1);

  // check the only locality and its endpoints.
  const auto& locality_response = cluster_response.locality_endpoints_health(0);
  EXPECT_EQ(locality_response.locality().sub_zone(), "hobbiton");
  ASSERT_EQ(locality_response.endpoints_health_size(), 2);
  EXPECT_TRUE(checkEndpointHealthResponse(locality_response.endpoints_health(0),
                                          envoy::config::core::v3::UNHEALTHY,
                                          host_upstream_->localAddress()));

  checkCounters(1, 2, 1, 1);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests that Envoy can HTTP health check two hosts that are in the same cluster, and
// different localities and report back the correct health statuses.
TEST_P(HdsIntegrationTest, TwoEndpointsDifferentLocality) {
  initialize();
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);

  // Add endpoint
  auto* health_check = server_health_check_specifier_.mutable_cluster_health_checks(0);

  Network::Utility::addressToProtobufAddress(
      *host2_upstream_->localAddress(),
      *health_check->add_locality_endpoints()->add_endpoints()->mutable_address());
  health_check->mutable_locality_endpoints(1)->mutable_locality()->set_region("plakias");
  health_check->mutable_locality_endpoints(1)->mutable_locality()->set_zone("fragokastelo");
  health_check->mutable_locality_endpoints(1)->mutable_locality()->set_sub_zone("emplisi");

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends health check messages to two endpoints
  healthcheckEndpoints("anna");

  // Endpoint responds to the health check
  host_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "404"}}, false);
  host_stream_->encodeData(1024, true);
  host2_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  host2_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  ASSERT_TRUE(waitForClusterHealthResponse(envoy::config::core::v3::HEALTHY,
                                           host2_upstream_->localAddress(), 0, 1, 0));

  ASSERT_EQ(response_.endpoint_health_response().cluster_endpoints_health_size(), 1);

  // store cluster response info for easier reference.
  const auto& cluster_response = response_.endpoint_health_response().cluster_endpoints_health(0);

  // Check cluster has correct name and number of localities (2)
  EXPECT_EQ(cluster_response.cluster_name(), "anna");
  ASSERT_EQ(cluster_response.locality_endpoints_health_size(), 2);

  // check first locality.
  const auto& locality_resp0 = cluster_response.locality_endpoints_health(0);
  EXPECT_EQ(locality_resp0.locality().sub_zone(), "hobbiton");
  ASSERT_EQ(locality_resp0.endpoints_health_size(), 1);
  EXPECT_TRUE(checkEndpointHealthResponse(locality_resp0.endpoints_health(0),
                                          envoy::config::core::v3::UNHEALTHY,
                                          host_upstream_->localAddress()));

  // check second locality.
  const auto& locality_resp1 = cluster_response.locality_endpoints_health(1);
  EXPECT_EQ(locality_resp1.locality().sub_zone(), "emplisi");
  ASSERT_EQ(locality_resp1.endpoints_health_size(), 1);

  checkCounters(1, 2, 1, 1);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests that Envoy can HTTP health check two hosts that are in different clusters, and
// report back the correct health statuses.
TEST_P(HdsIntegrationTest, TwoEndpointsDifferentClusters) {
  initialize();
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);

  // Add Second Cluster
  server_health_check_specifier_.add_cluster_health_checks()->MergeFrom(createSecondCluster("cat"));

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends health check messages to two endpoints
  healthcheckEndpoints("cat");

  // Endpoint responds to the health check
  host_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "404"}}, false);
  host_stream_->encodeData(1024, true);
  host2_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  host2_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  ASSERT_TRUE(waitForClusterHealthResponse(envoy::config::core::v3::HEALTHY,
                                           host2_upstream_->localAddress(), 1, 0, 0));

  ASSERT_EQ(response_.endpoint_health_response().cluster_endpoints_health_size(), 2);

  // store cluster response info for easier reference.
  const auto& cluster_resp0 = response_.endpoint_health_response().cluster_endpoints_health(0);
  const auto& cluster_resp1 = response_.endpoint_health_response().cluster_endpoints_health(1);

  // check cluster info and sizes.
  EXPECT_EQ(cluster_resp0.cluster_name(), "anna");
  ASSERT_EQ(cluster_resp0.locality_endpoints_health_size(), 1);
  EXPECT_EQ(cluster_resp1.cluster_name(), "cat");
  ASSERT_EQ(cluster_resp1.locality_endpoints_health_size(), 1);

  // store locality response info for easier reference.
  const auto& locality_resp0 = cluster_resp0.locality_endpoints_health(0);
  const auto& locality_resp1 = cluster_resp1.locality_endpoints_health(0);

  // check locality info and sizes.
  EXPECT_EQ(locality_resp0.locality().sub_zone(), "hobbiton");
  ASSERT_EQ(locality_resp0.endpoints_health_size(), 1);
  EXPECT_EQ(locality_resp1.locality().sub_zone(), "paris");
  ASSERT_EQ(locality_resp1.endpoints_health_size(), 1);

  // check endpoints.
  EXPECT_TRUE(checkEndpointHealthResponse(locality_resp0.endpoints_health(0),
                                          envoy::config::core::v3::UNHEALTHY,
                                          host_upstream_->localAddress()));

  checkCounters(1, 2, 0, 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cat.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cat.health_check.failure")->value());

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests Envoy HTTP health checking a single endpoint, receiving an update
// message from the management server and health checking a new endpoint
TEST_P(HdsIntegrationTest, TestUpdateMessage) {
  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends a health check message to an endpoint
  healthcheckEndpoints();

  // Endpoint responds to the health check
  host_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  host_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  waitForEndpointHealthResponse(envoy::config::core::v3::HEALTHY);

  checkCounters(1, 2, 1, 0);

  cleanupHostConnections();

  // New HealthCheckSpecifier message
  envoy::service::health::v3::HealthCheckSpecifier new_message;
  new_message.mutable_interval()->set_nanos(100000000); // 0.1 seconds

  auto* health_check = new_message.add_cluster_health_checks();

  health_check->set_cluster_name("cat");
  Network::Utility::addressToProtobufAddress(
      *host2_upstream_->localAddress(),
      *health_check->add_locality_endpoints()->add_endpoints()->mutable_address());

  health_check->mutable_locality_endpoints(0)->mutable_locality()->set_region("matala");
  health_check->mutable_locality_endpoints(0)->mutable_locality()->set_zone("tilburg");
  health_check->mutable_locality_endpoints(0)->mutable_locality()->set_sub_zone("rivendell");

  health_check->add_health_checks()->mutable_timeout()->set_seconds(MaxTimeout);
  health_check->mutable_health_checks(0)->mutable_interval()->set_seconds(MaxTimeout);
  health_check->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(2);
  health_check->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(2);
  health_check->mutable_health_checks(0)->mutable_grpc_health_check();
  health_check->mutable_health_checks(0)
      ->mutable_http_health_check()
      ->set_hidden_envoy_deprecated_use_http2(false);
  health_check->mutable_health_checks(0)->mutable_http_health_check()->set_path("/healthcheck");

  // Server asks for health checking with the new message
  hds_stream_->sendGrpcMessage(new_message);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends a health check message to an endpoint
  ASSERT_TRUE(host2_upstream_->waitForHttpConnection(*dispatcher_, host2_fake_connection_));
  ASSERT_TRUE(host2_fake_connection_->waitForNewStream(*dispatcher_, host2_stream_));
  ASSERT_TRUE(host2_stream_->waitForEndStream(*dispatcher_));

  // Endpoint responds to the health check
  host2_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "404"}}, false);
  host2_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, response_));
  while (!checkEndpointHealthResponse(response_.endpoint_health_response().endpoints_health(0),
                                      envoy::config::core::v3::UNHEALTHY,
                                      host2_upstream_->localAddress())) {
    ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, response_));
  }

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests Envoy HTTP health checking a single endpoint, receiving an update
// message from the management server and reporting in a new interval
TEST_P(HdsIntegrationTest, TestUpdateChangesTimer) {
  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  healthcheckEndpoints();

  // an update should be received after interval
  ASSERT_TRUE(
      hds_stream_->waitForGrpcMessage(*dispatcher_, response_, std::chrono::milliseconds(250)));

  // New HealthCheckSpecifier message
  server_health_check_specifier_.mutable_interval()->set_nanos(300000000); // 0.3 seconds

  // Server asks for health checking with the new message
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // A response should not be received until the new timer is completed
  ASSERT_FALSE(
      hds_stream_->waitForGrpcMessage(*dispatcher_, response_, std::chrono::milliseconds(100)));
  // Response should be received now
  ASSERT_TRUE(
      hds_stream_->waitForGrpcMessage(*dispatcher_, response_, std::chrono::milliseconds(400)));

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Tests Envoy HTTP health checking a single endpoint when interval hasn't been defined
TEST_P(HdsIntegrationTest, TestDefaultTimer) {
  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);
  server_health_check_specifier_.clear_interval();
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  healthcheckEndpoints();

  // an update should be received after interval
  ASSERT_TRUE(
      hds_stream_->waitForGrpcMessage(*dispatcher_, response_, std::chrono::milliseconds(2500)));

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Health checks a single endpoint over TLS with HTTP/2
TEST_P(HdsIntegrationTest, SingleEndpointHealthyTlsHttp2) {
  // Change member variable to specify host streams to have tls transport socket.
  tls_hosts_ = true;

  // Change hosts to operate over HTTP/2 instead of default HTTP.
  http_conn_type_ = FakeHttpConnection::Type::HTTP2;

  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));
  EXPECT_EQ(envoy_msg_.health_check_request().capability().health_check_protocols(0),
            envoy::service::health::v3::Capability::HTTP);

  // Server asks for health checking
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP2, true);
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends a health check message to an endpoint
  healthcheckEndpoints();

  // Endpoint responds to the health check
  host_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  host_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  waitForEndpointHealthResponse(envoy::config::core::v3::HEALTHY);

  checkCounters(1, 2, 1, 0);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Health checks a single endpoint over TLS with HTTP/1
TEST_P(HdsIntegrationTest, SingleEndpointHealthyTlsHttp1) {
  // Change member variable to specify host streams to have tls transport socket.
  tls_hosts_ = true;

  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));
  EXPECT_EQ(envoy_msg_.health_check_request().capability().health_check_protocols(0),
            envoy::service::health::v3::Capability::HTTP);

  // Server asks for health checking
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, true);
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends a health check message to an endpoint
  healthcheckEndpoints();

  // Endpoint responds to the health check
  host_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  host_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  waitForEndpointHealthResponse(envoy::config::core::v3::HEALTHY);

  checkCounters(1, 2, 1, 0);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

// Attempts to health check a TLS endpoint over plaintext, which should fail.
TEST_P(HdsIntegrationTest, SingleEndpointUnhealthyTlsMissingSocketMatch) {
  // Make the endpoints expect communication over TLS.
  tls_hosts_ = true;

  initialize();

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));
  EXPECT_EQ(envoy_msg_.health_check_request().capability().health_check_protocols(0),
            envoy::service::health::v3::Capability::HTTP);

  // Make the specifier not have the TLS socket matches, so it will try to connect over plaintext.
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);

  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends a health check message to an endpoint
  ASSERT_TRUE(host_upstream_->waitForRawConnection(host_fake_raw_connection_));

  // Endpoint doesn't respond to the health check
  ASSERT_TRUE(host_fake_raw_connection_->waitForDisconnect());

  // Receive updates until the one we expect arrives. This should be UNHEALTHY and not TIMEOUT,
  // because TIMEOUT occurs in the situation where there is no response from the endpoint. In this
  // case, the endpoint does respond but it is over TLS, and HDS is trying to parse it as plaintext.
  // It does not recognize the malformed plaintext, so it is considered a failure and UNHEALTHY is
  // set.
  waitForEndpointHealthResponse(envoy::config::core::v3::UNHEALTHY);

  checkCounters(1, 2, 0, 1);

  // Clean up connections
  cleanupHostConnections();
  cleanupHdsConnection();
}

TEST_P(HdsIntegrationTest, UpdateEndpoints) {
  initialize();
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);

  // Add Second Cluster.
  server_health_check_specifier_.add_cluster_health_checks()->MergeFrom(createSecondCluster("cat"));

  // Server <--> Envoy
  waitForHdsStream();
  ASSERT_TRUE(hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_));

  // Server asks for health checking
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Envoy sends health check messages to two endpoints
  healthcheckEndpoints("cat");

  // Endpoint responds to the health check
  host_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "404"}}, false);
  host_stream_->encodeData(1024, true);
  host2_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  host2_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  ASSERT_TRUE(waitForClusterHealthResponse(envoy::config::core::v3::HEALTHY,
                                           host2_upstream_->localAddress(), 1, 0, 0));

  ASSERT_EQ(response_.endpoint_health_response().cluster_endpoints_health_size(), 2);

  // store cluster response info for easier reference.
  const auto& cluster_resp0 = response_.endpoint_health_response().cluster_endpoints_health(0);
  const auto& cluster_resp1 = response_.endpoint_health_response().cluster_endpoints_health(1);

  // check cluster info and sizes.
  EXPECT_EQ(cluster_resp0.cluster_name(), "anna");
  ASSERT_EQ(cluster_resp0.locality_endpoints_health_size(), 1);
  EXPECT_EQ(cluster_resp1.cluster_name(), "cat");
  ASSERT_EQ(cluster_resp1.locality_endpoints_health_size(), 1);

  // store locality response info for easier reference.
  const auto& locality_resp0 = cluster_resp0.locality_endpoints_health(0);
  const auto& locality_resp1 = cluster_resp1.locality_endpoints_health(0);

  // check locality info and sizes.
  EXPECT_EQ(locality_resp0.locality().sub_zone(), "hobbiton");
  ASSERT_EQ(locality_resp0.endpoints_health_size(), 1);
  EXPECT_EQ(locality_resp1.locality().sub_zone(), "paris");
  ASSERT_EQ(locality_resp1.endpoints_health_size(), 1);

  // Check endpoints.
  EXPECT_TRUE(checkEndpointHealthResponse(locality_resp0.endpoints_health(0),
                                          envoy::config::core::v3::UNHEALTHY,
                                          host_upstream_->localAddress()));

  checkCounters(1, 2, 0, 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cat.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cat.health_check.failure")->value());

  // Create new specifier that removes the second cluster, and adds an endpoint to the first.
  server_health_check_specifier_ =
      makeHttpHealthCheckSpecifier(envoy::type::v3::CodecClientType::HTTP1, false);
  Network::Utility::addressToProtobufAddress(
      *host2_upstream_->localAddress(),
      *server_health_check_specifier_.mutable_cluster_health_checks(0)
           ->mutable_locality_endpoints(0)
           ->add_endpoints()
           ->mutable_address());

  // Reset second endpoint for usage in our cluster.
  ASSERT_TRUE(host2_fake_connection_->close());
  ASSERT_TRUE(host2_fake_connection_->waitForDisconnect());

  // Send new specifier.
  hds_stream_->sendGrpcMessage(server_health_check_specifier_);
  // TODO: add stats reporting and verification for Clusters added/removed/reused and Endpoints
  // added/removed/reused.
  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);

  // Set up second endpoint again.
  ASSERT_TRUE(host2_upstream_->waitForHttpConnection(*dispatcher_, host2_fake_connection_));
  ASSERT_TRUE(host2_fake_connection_->waitForNewStream(*dispatcher_, host2_stream_));
  ASSERT_TRUE(host2_stream_->waitForEndStream(*dispatcher_));
  EXPECT_EQ(host2_stream_->headers().getPathValue(), "/healthcheck");
  EXPECT_EQ(host2_stream_->headers().getMethodValue(), "GET");
  EXPECT_EQ(host2_stream_->headers().getHostValue(), "anna");

  // Endpoints respond to the health check
  host2_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  host2_stream_->encodeData(1024, true);

  // Receive updates until the one we expect arrives
  ASSERT_TRUE(waitForClusterHealthResponse(envoy::config::core::v3::HEALTHY,
                                           host2_upstream_->localAddress(), 0, 0, 1));

  // Ensure we have at least one cluster before trying to read it.
  ASSERT_EQ(response_.endpoint_health_response().cluster_endpoints_health_size(), 1);

  // store cluster response info for easier reference.
  const auto& cluster_response = response_.endpoint_health_response().cluster_endpoints_health(0);

  // Check cluster has correct name and number of localities (1)
  EXPECT_EQ(cluster_response.cluster_name(), "anna");
  ASSERT_EQ(cluster_response.locality_endpoints_health_size(), 1);

  // check the only locality and its endpoints.
  const auto& locality_response = cluster_response.locality_endpoints_health(0);
  EXPECT_EQ(locality_response.locality().sub_zone(), "hobbiton");
  ASSERT_EQ(locality_response.endpoints_health_size(), 2);
  EXPECT_TRUE(checkEndpointHealthResponse(locality_response.endpoints_health(0),
                                          envoy::config::core::v3::UNHEALTHY,
                                          host_upstream_->localAddress()));

  cleanupHostConnections();
  cleanupHdsConnection();
}

} // namespace
} // namespace Envoy
