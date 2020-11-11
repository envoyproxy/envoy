#include <memory>

#include "envoy/config/core/v3/health_check.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/http2/http2_frame.h"
#include "test/common/upstream/utility.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Integration tests for active health checking.
// The tests fetch the cluster configuration using CDS in order to actively start health
// checking after Envoy and the hosts are initialized.
class HealthCheckIntegrationTestBase : public Event::TestUsingSimulatedTime,
                                       public HttpIntegrationTest {
public:
  HealthCheckIntegrationTestBase(
      Network::Address::IpVersion ip_version,
      FakeHttpConnection::Type upstream_protocol = FakeHttpConnection::Type::HTTP2)
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ip_version,
                            ConfigHelper::discoveredClustersBootstrap("GRPC")),
        ip_version_(ip_version), upstream_protocol_(upstream_protocol) {}

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

    registerTestServerPorts({"http"});

    // Create the regular (i.e. not an xDS server) upstreams. We create them manually here after
    // initialize() because finalize() expects all fake_upstreams_ to correspond to a static
    // cluster in the bootstrap config - which we don't want since we're using dynamic CDS.
    for (auto& cluster : clusters_) {
      cluster.host_upstream_ = std::make_unique<FakeUpstream>(0, upstream_protocol_, version_,
                                                              timeSystem(), enable_half_close_);
      cluster.cluster_ = ConfigHelper::buildStaticCluster(
          cluster.name_, cluster.host_upstream_->localAddress()->ip()->port(),
          Network::Test::getLoopbackAddressString(ip_version_));
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

  // Adds an active health check specifier to the given cluster.
  envoy::config::core::v3::HealthCheck*
  addHealthCheck(envoy::config::cluster::v3::Cluster& cluster) {
    // Add general health check specifier to the cluster.
    auto* health_check = cluster.add_health_checks();
    health_check->mutable_timeout()->set_seconds(30);
    health_check->mutable_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_no_traffic_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_unhealthy_threshold()->set_value(1);
    health_check->mutable_healthy_threshold()->set_value(1);
    return health_check;
  }

  // The number of clusters and their names must match the clusters in the CDS integration test
  // configuration.
  static constexpr size_t clusters_num_ = 2;
  std::array<ClusterData, clusters_num_> clusters_{{{"cluster_1"}, {"cluster_2"}}};
  Network::Address::IpVersion ip_version_;
  FakeHttpConnection::Type upstream_protocol_;
};

struct HttpHealthCheckIntegrationTestParams {
  Network::Address::IpVersion ip_version;
  FakeHttpConnection::Type upstream_protocol;
};

class HttpHealthCheckIntegrationTest
    : public testing::TestWithParam<HttpHealthCheckIntegrationTestParams>,
      public HealthCheckIntegrationTestBase {
public:
  HttpHealthCheckIntegrationTest()
      : HealthCheckIntegrationTestBase(GetParam().ip_version, GetParam().upstream_protocol) {}

  // Returns the 4 combinations for testing:
  // [HTTP1, HTTP2] x [IPv4, IPv6]
  static std::vector<HttpHealthCheckIntegrationTestParams>
  getHttpHealthCheckIntegrationTestParams() {
    std::vector<HttpHealthCheckIntegrationTestParams> ret;

    for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
      for (auto upstream_protocol :
           {FakeHttpConnection::Type::HTTP1, FakeHttpConnection::Type::HTTP2}) {
        ret.push_back(HttpHealthCheckIntegrationTestParams{ip_version, upstream_protocol});
      }
    }
    return ret;
  }

  static std::string httpHealthCheckTestParamsToString(
      const ::testing::TestParamInfo<HttpHealthCheckIntegrationTestParams>& params) {
    return absl::StrCat(
        (params.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
        (params.param.upstream_protocol == FakeHttpConnection::Type::HTTP2 ? "Http2Upstream"
                                                                           : "HttpUpstream"));
  }

  void TearDown() override {
    cleanupHostConnections();
    cleanUpXdsConnection();
  }

  // Adds a HTTP active health check specifier to the given cluster, and waits for the first health
  // check probe to be received.
  void initHttpHealthCheck(uint32_t cluster_idx) {
    const envoy::type::v3::CodecClientType codec_client_type =
        (FakeHttpConnection::Type::HTTP1 == upstream_protocol_)
            ? envoy::type::v3::CodecClientType::HTTP1
            : envoy::type::v3::CodecClientType::HTTP2;

    auto& cluster_data = clusters_[cluster_idx];
    auto* health_check = addHealthCheck(cluster_data.cluster_);
    health_check->mutable_http_health_check()->set_path("/healthcheck");
    health_check->mutable_http_health_check()->set_codec_client_type(codec_client_type);

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
};

INSTANTIATE_TEST_SUITE_P(
    IpHttpVersions, HttpHealthCheckIntegrationTest,
    testing::ValuesIn(HttpHealthCheckIntegrationTest::getHttpHealthCheckIntegrationTestParams()),
    HttpHealthCheckIntegrationTest::httpHealthCheckTestParamsToString);

// Tests that a healthy endpoint returns a valid HTTP health check response.
TEST_P(HttpHealthCheckIntegrationTest, SingleEndpointHealthyHttp) {
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
}

// Tests that an unhealthy endpoint returns a valid HTTP health check response.
TEST_P(HttpHealthCheckIntegrationTest, SingleEndpointUnhealthyHttp) {
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
}

// Tests that no HTTP health check response results in timeout and unhealthy endpoint.
TEST_P(HttpHealthCheckIntegrationTest, SingleEndpointTimeoutHttp) {
  const uint32_t cluster_idx = 0;
  initialize();
  initHttpHealthCheck(cluster_idx);

  // Increase time until timeout (30s).
  timeSystem().advanceTimeWait(std::chrono::seconds(30));

  // Endpoint doesn't reply, and a healthcheck failure occurs (due to timeout).
  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

class TcpHealthCheckIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HealthCheckIntegrationTestBase {
public:
  TcpHealthCheckIntegrationTest() : HealthCheckIntegrationTestBase(GetParam()) {}

  void TearDown() override {
    cleanupHostConnections();
    cleanUpXdsConnection();
  }

  // Adds a TCP active health check specifier to the given cluster, and waits for the first health
  // check probe to be received.
  void initTcpHealthCheck(uint32_t cluster_idx) {
    auto& cluster_data = clusters_[cluster_idx];
    auto health_check = addHealthCheck(cluster_data.cluster_);
    health_check->mutable_tcp_health_check()->mutable_send()->set_text("50696E67"); // "Ping"
    health_check->mutable_tcp_health_check()->add_receive()->set_text("506F6E67");  // "Pong"

    // Introduce the cluster using compareDiscoveryRequest / sendDiscoveryResponse.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {cluster_data.cluster_}, {cluster_data.cluster_}, {}, "55");

    // Wait for upstream to receive TCP HC request.
    ASSERT_TRUE(
        cluster_data.host_upstream_->waitForRawConnection(cluster_data.host_fake_raw_connection_));
    ASSERT_TRUE(cluster_data.host_fake_raw_connection_->waitForData(
        FakeRawConnection::waitForInexactMatch("Ping")));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TcpHealthCheckIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Tests that a healthy endpoint returns a valid TCP health check response.
TEST_P(TcpHealthCheckIntegrationTest, SingleEndpointHealthyTcp) {
  const uint32_t cluster_idx = 0;
  initialize();
  initTcpHealthCheck(cluster_idx);

  AssertionResult result = clusters_[cluster_idx].host_fake_raw_connection_->write("Pong");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

// Tests that an invalid response fails the health check.
TEST_P(TcpHealthCheckIntegrationTest, SingleEndpointWrongResponseTcp) {
  const uint32_t cluster_idx = 0;
  initialize();
  initTcpHealthCheck(cluster_idx);

  // Send the wrong reply ("Pong" is expected).
  AssertionResult result = clusters_[cluster_idx].host_fake_raw_connection_->write("Poong");
  RELEASE_ASSERT(result, result.message());

  // Envoy will wait until timeout occurs because no correct reply was received.
  // Increase time until timeout (30s).
  timeSystem().advanceTimeWait(std::chrono::seconds(30));

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

// Tests that no TCP health check response results in timeout and unhealthy endpoint.
TEST_P(TcpHealthCheckIntegrationTest, SingleEndpointTimeoutTcp) {
  const uint32_t cluster_idx = 0;
  initialize();
  initTcpHealthCheck(cluster_idx);

  // Increase time until timeout (30s).
  timeSystem().advanceTimeWait(std::chrono::seconds(30));

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

class GrpcHealthCheckIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HealthCheckIntegrationTestBase {
public:
  GrpcHealthCheckIntegrationTest() : HealthCheckIntegrationTestBase(GetParam()) {}

  void TearDown() override {
    cleanupHostConnections();
    cleanUpXdsConnection();
  }

  // Adds a gRPC active health check specifier to the given cluster, and waits for the first health
  // check probe to be received.
  void initGrpcHealthCheck(uint32_t cluster_idx) {
    auto& cluster_data = clusters_[cluster_idx];
    auto health_check = addHealthCheck(cluster_data.cluster_);
    health_check->mutable_grpc_health_check();

    // Introduce the cluster using compareDiscoveryRequest / sendDiscoveryResponse.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {cluster_data.cluster_}, {cluster_data.cluster_}, {}, "55");

    // Wait for upstream to receive HC request.
    grpc::health::v1::HealthCheckRequest request;
    ASSERT_TRUE(cluster_data.host_upstream_->waitForHttpConnection(
        *dispatcher_, cluster_data.host_fake_connection_));
    ASSERT_TRUE(cluster_data.host_fake_connection_->waitForNewStream(*dispatcher_,
                                                                     cluster_data.host_stream_));
    ASSERT_TRUE(cluster_data.host_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(cluster_data.host_stream_->waitForEndStream(*dispatcher_));

    EXPECT_EQ(cluster_data.host_stream_->headers().getPathValue(), "/grpc.health.v1.Health/Check");
    EXPECT_EQ(cluster_data.host_stream_->headers().getContentTypeValue(),
              Http::Headers::get().ContentTypeValues.Grpc);
    EXPECT_EQ(cluster_data.host_stream_->headers().getHostValue(), cluster_data.name_);
  }

  // Send a gRPC message with the given headers and health check response message.
  void sendGrpcResponse(uint32_t cluster_idx,
                        const Http::TestResponseHeaderMapImpl& response_headers,
                        const grpc::health::v1::HealthCheckResponse& health_check_response) {
    clusters_[cluster_idx].host_stream_->encodeHeaders(response_headers, false);
    clusters_[cluster_idx].host_stream_->sendGrpcMessage(health_check_response);
    clusters_[cluster_idx].host_stream_->finishGrpcStream(Grpc::Status::WellKnownGrpcStatus::Ok);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GrpcHealthCheckIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Tests that a healthy endpoint returns a valid gRPC health check response.
TEST_P(GrpcHealthCheckIntegrationTest, SingleEndpointServingGrpc) {
  initialize();

  const uint32_t cluster_idx = 0;
  initGrpcHealthCheck(cluster_idx);

  // Endpoint responds to the health check
  grpc::health::v1::HealthCheckResponse response;
  response.set_status(grpc::health::v1::HealthCheckResponse::SERVING);
  // Send a grpc response.
  sendGrpcResponse(
      cluster_idx,
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-type", Http::Headers::get().ContentTypeValues.Grpc}},
      response);

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

// Tests that an unhealthy endpoint returns a valid gRPC health check response.
TEST_P(GrpcHealthCheckIntegrationTest, SingleEndpointNotServingGrpc) {
  initialize();

  const uint32_t cluster_idx = 0;
  initGrpcHealthCheck(cluster_idx);

  // Endpoint responds to the health check
  grpc::health::v1::HealthCheckResponse response;
  response.set_status(grpc::health::v1::HealthCheckResponse::NOT_SERVING);
  // Send a grpc response.
  sendGrpcResponse(
      cluster_idx,
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-type", Http::Headers::get().ContentTypeValues.Grpc}},
      response);

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

// Tests that no gRPC health check response results in timeout and unhealthy endpoint.
TEST_P(GrpcHealthCheckIntegrationTest, SingleEndpointTimeoutGrpc) {
  initialize();

  const uint32_t cluster_idx = 0;
  initGrpcHealthCheck(cluster_idx);

  // Increase time until timeout (30s).
  timeSystem().advanceTimeWait(std::chrono::seconds(30));

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

// Tests that a gRPC health check response that returns SERVICE_UNKNOWN is
// properly handled (see https://github.com/envoyproxy/envoy/issues/10825).
TEST_P(GrpcHealthCheckIntegrationTest, SingleEndpointServiceUnknownGrpc) {
  initialize();

  const uint32_t cluster_idx = 0;
  initGrpcHealthCheck(cluster_idx);

  // Endpoint responds to the health check
  grpc::health::v1::HealthCheckResponse response;
  response.set_status(grpc::health::v1::HealthCheckResponse::SERVICE_UNKNOWN);
  // Send a grpc response.
  sendGrpcResponse(
      cluster_idx,
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-type", Http::Headers::get().ContentTypeValues.Grpc}},
      response);

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

// Tests that a gRPC health check response that returns an unknown status
// response is properly handled.
TEST_P(GrpcHealthCheckIntegrationTest, SingleEndpointUnknownStatusGrpc) {
  initialize();

  const uint32_t cluster_idx = 0;
  initGrpcHealthCheck(cluster_idx);

  // Endpoint responds to the health check
  grpc::health::v1::HealthCheckResponse response;
  response.set_status(static_cast<grpc::health::v1::HealthCheckResponse::ServingStatus>(123));
  // Send a grpc response.
  sendGrpcResponse(
      cluster_idx,
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-type", Http::Headers::get().ContentTypeValues.Grpc}},
      response);

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

} // namespace
} // namespace Envoy
