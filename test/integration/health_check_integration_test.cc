#include <memory>

#include "envoy/config/core/v3/health_check.pb.h"

#include "test/common/http/http2/http2_frame.h"
#include "test/common/upstream/utility.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Integration tests for active health checking.
// The tests fetch the cluster configuration using CDS in order to actively start health
// checking after Envoy and the hosts are initialized.
class HealthCheckIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  HealthCheckIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam(),
                            ConfigHelper::discoveredClustersBootstrap("GRPC")) {}

  enum HealthCheckType { HTTP, TCP, GRPC };

  // Per-cluster information including the fake connection and stream.
  struct ClusterData {
    const std::string name_;
    envoy::config::cluster::v3::Cluster cluster_;
    FakeUpstreamPtr host_upstream_;
    FakeStreamPtr host_stream_;
    FakeHttpConnectionPtr host_fake_connection_;
    FakeRawConnectionPtr host_fake_raw_connection_;

    ClusterData(const std::string name) : name_(name) {}
  };

  void TearDown() override { cleanUpXdsConnection(); }

  void initialize() override {
    // The endpoints and their configuration is received as part of a CDS response, and not
    // statically defined clusters with active health-checking because in an integration test the
    // hosts will be able to reply to the health-check requests only after the tests framework
    // initialization has finished. This follows the same initialization procedure that is executed
    // in the CDS integration tests.

    use_lds_ = false;
    // Controls how many fake_upstreams_.emplace_back(new FakeUpstream) will happen in
    // BaseIntegrationTest::createUpstreams() (which is part of initialize()).
    // Make sure this number matches the size of the 'clusters' repeated field in the bootstrap
    // config that you use!
    setUpstreamCount(1);                                  // the CDS cluster
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2); // CDS uses gRPC uses HTTP2.

    // HttpIntegrationTest::initialize() does many things:
    // 1) It appends to fake_upstreams_ as many as you asked for via setUpstreamCount().
    // 2) It updates your bootstrap config with the ports your fake upstreams are actually listening
    //    on (since you're supposed to leave them as 0).
    // 3) It creates and starts an IntegrationTestServer - the thing that wraps the almost-actual
    //    Envoy used in the tests.
    // 4) Bringing up the server usually entails waiting to ensure that any listeners specified in
    //    the bootstrap config have come up, and registering them in a port map (see lookupPort()).
    //    However, this test needs to defer all of that to later.
    defer_listener_finalization_ = true;
    HttpIntegrationTest::initialize();

    // Let Envoy establish its connection to the CDS server.
    acceptXdsConnection();

    // Expect 1 for the statically specified CDS server.
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 1);

    // Register the xds server port in the test framework's downstream listener port map.
    // test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});

    // Create the regular (i.e. not an xDS server) upstreams. We create them manually here after
    // initialize() because finalize() expects all fake_upstreams_ to correspond to a static
    // cluster in the bootstrap config - which we don't want since we're using dynamic CDS.
    const FakeHttpConnection::Type http_conn_type =
        (envoy::type::v3::CodecClientType::HTTP1 == codec_client_type_)
            ? FakeHttpConnection::Type::HTTP1
            : FakeHttpConnection::Type::HTTP2;
    for (auto& cluster : clusters_) {
      cluster.host_upstream_ = std::make_unique<FakeUpstream>(0, http_conn_type, version_,
                                                              timeSystem(), enable_half_close_);
      cluster.cluster_ = ConfigHelper::buildStaticCluster(
          cluster.name_, cluster.host_upstream_->localAddress()->ip()->port(),
          Network::Test::getLoopbackAddressString(GetParam()));
    }
  }

  void acceptXdsConnection() {
    AssertionResult result = // xds_connection_ is filled with the new FakeHttpConnection.
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  // Closes the connections to the fake hosts.
  void cleanupHostConnections() {
    for (auto& cluster : clusters_) {
      auto& host_fake_connection = cluster.host_fake_connection_;
      if (host_fake_connection != nullptr) {
        AssertionResult result = host_fake_connection->close();
        RELEASE_ASSERT(result, result.message());
        result = host_fake_connection->waitForDisconnect();
        RELEASE_ASSERT(result, result.message());
      }
    }
  }

  // Adds an active health check specifier to the given cluster with the given timeout.
  envoy::config::core::v3::HealthCheck* addHealthCheck(envoy::config::cluster::v3::Cluster& cluster,
                                                       uint32_t timeout) {
    // Add general health check specifier to the cluster.
    auto* health_check = cluster.add_health_checks();
    health_check->mutable_timeout()->set_seconds(timeout);
    health_check->mutable_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_no_traffic_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_unhealthy_threshold()->set_value(1);
    health_check->mutable_healthy_threshold()->set_value(1);
    return health_check;
  }

  // Adds a HTTP active health check specifier to the given cluster with the given timeout,
  // and waits for the first health check probe to be received.
  void initHttpHealthCheck(uint32_t cluster_idx, uint32_t timeout = 30) {
    auto& cluster_data = clusters_[cluster_idx];
    auto* health_check = addHealthCheck(cluster_data.cluster_, timeout);
    health_check->mutable_http_health_check()->set_path("/healthcheck");
    health_check->mutable_http_health_check()->set_codec_client_type(codec_client_type_);

    // Introduce the cluster using compareDiscoveryRequest / sendDiscoveryResponse.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {cluster_data.cluster_}, {cluster_data.cluster_}, {}, "55");

    // Wait for upstream to receive health check request.
    ASSERT_TRUE(cluster_data.host_upstream_->waitForHttpConnection(
        *dispatcher_, cluster_data.host_fake_connection_));
    ASSERT_TRUE(cluster_data.host_fake_connection_->waitForNewStream(*dispatcher_,
                                                                     cluster_data.host_stream_));
    ASSERT_TRUE(cluster_data.host_stream_->waitForEndStream(*dispatcher_));

    EXPECT_EQ(cluster_data.host_stream_->headers().getPathValue(), "/healthcheck");
    EXPECT_EQ(cluster_data.host_stream_->headers().getMethodValue(), "GET");
    EXPECT_EQ(cluster_data.host_stream_->headers().getHostValue(), cluster_data.name_);
  }

  // Tests that a healthy endpoint returns a valid HTTP health check response.
  // HTTP tests are defined here to check both HTTP1 and HTTP2.
  void singleEndpointHealthyHttp(envoy::type::v3::CodecClientType codec_client_type) {
    codec_client_type_ = codec_client_type;

    const uint32_t cluster_idx = 0;
    initialize();
    initHttpHealthCheck(cluster_idx);

    // Endpoint responds with healthy status to the health check.
    clusters_[cluster_idx].host_stream_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    clusters_[cluster_idx].host_stream_->encodeData(1024, true);

    // Verify that Envoy detected the health check response.
    test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 1);
    EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
    EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());

    // Clean up connections.
    cleanupHostConnections();
  }

  // Tests that an unhealthy endpoint returns a valid HTTP health check response.
  // HTTP tests are defined here to check both HTTP1 and HTTP2.
  void singleEndpointUnhealthyHttp(envoy::type::v3::CodecClientType codec_client_type) {
    codec_client_type_ = codec_client_type;

    const uint32_t cluster_idx = 0;
    initialize();
    initHttpHealthCheck(cluster_idx);

    // Endpoint responds to the health check with unhealthy status.
    clusters_[cluster_idx].host_stream_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);
    clusters_[cluster_idx].host_stream_->encodeData(1024, true);

    test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
    EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
    EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());

    // Clean up connections.
    cleanupHostConnections();
  }

  // Tests that no HTTP health check response results in timeout and unhealthy endpoint.
  // HTTP tests are defined here to check both HTTP1 and HTTP2.
  void singleEndpointTimeoutHttp(envoy::type::v3::CodecClientType codec_client_type) {
    codec_client_type_ = codec_client_type;

    const uint32_t cluster_idx = 0;
    initialize();
    // Set timeout to 1 second, so the waiting will not be long.
    initHttpHealthCheck(cluster_idx, 1);

    // Endpoint doesn't reply, and a healthcheck failure occurs (due to timeout).
    test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
    EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
    EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());

    // Clean up connections
    cleanupHostConnections();
  }

  // The number of clusters and their names must match the clusters in the CDS integration test
  // configuration.
  static constexpr size_t clusters_num_ = 2;
  std::array<ClusterData, clusters_num_> clusters_{{{"cluster_1"}, {"cluster_2"}}};
  envoy::type::v3::CodecClientType codec_client_type_{envoy::type::v3::HTTP2};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HealthCheckIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Tests that a healthy endpoint returns a valid HTTP1 health check response.
TEST_P(HealthCheckIntegrationTest, SingleEndpointHealthyHttp1) {
  singleEndpointHealthyHttp(envoy::type::v3::CodecClientType::HTTP1);
}

// Tests that a healthy endpoint returns a valid HTTP2 health check response.
TEST_P(HealthCheckIntegrationTest, SingleEndpointHealthyHttp2) {
  singleEndpointHealthyHttp(envoy::type::v3::CodecClientType::HTTP2);
}

// Tests that an unhealthy endpoint returns a valid HTTP1 health check response.
TEST_P(HealthCheckIntegrationTest, SingleEndpointUnhealthyHttp1) {
  singleEndpointUnhealthyHttp(envoy::type::v3::CodecClientType::HTTP1);
}

// Tests that an unhealthy endpoint returns a valid HTTP2 health check response.
TEST_P(HealthCheckIntegrationTest, SingleEndpointUnhealthyHttp2) {
  singleEndpointUnhealthyHttp(envoy::type::v3::CodecClientType::HTTP2);
}

// Tests that no HTTP1 health check response results in timeout and unhealthy endpoint.
TEST_P(HealthCheckIntegrationTest, SingleEndpointTimeoutHttp1) {
  singleEndpointTimeoutHttp(envoy::type::v3::CodecClientType::HTTP1);
}

// Tests that no HTTP2 health check response results in timeout and unhealthy endpoint.
TEST_P(HealthCheckIntegrationTest, SingleEndpointTimeoutHttp2) {
  singleEndpointTimeoutHttp(envoy::type::v3::CodecClientType::HTTP2);
}

} // namespace
} // namespace Envoy
