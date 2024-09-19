#include <memory>

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/type/v3/range.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/http2/http2_frame.h"
#include "test/common/upstream/utility.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Integration tests for active health checking.
// The tests fetch the cluster configuration using CDS in order to actively start health
// checking after Envoy and the hosts are initialized.
class HealthCheckIntegrationTestBase : public HttpIntegrationTest {
public:
  HealthCheckIntegrationTestBase(Network::Address::IpVersion ip_version,
                                 Http::CodecType upstream_protocol = Http::CodecType::HTTP2)
      : HttpIntegrationTest(Http::CodecType::HTTP2, ip_version,
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
    FakeUpstreamPtr external_host_upstream_;
    FakeStreamPtr external_host_stream_;
    FakeHttpConnectionPtr external_host_fake_connection_;
    FakeRawConnectionPtr external_host_fake_raw_connection_;

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
    setUpstreamCount(1);                         // the CDS cluster
    setUpstreamProtocol(Http::CodecType::HTTP2); // CDS uses gRPC uses HTTP2.

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
      auto config = upstreamConfig();
      config.upstream_protocol_ = upstream_protocol_;
      cluster.host_upstream_ = std::make_unique<FakeUpstream>(0, version_, config);
      cluster.external_host_upstream_ = std::make_unique<FakeUpstream>(0, version_, config);
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
  Http::CodecType upstream_protocol_;
};

struct HttpHealthCheckIntegrationTestParams {
  Network::Address::IpVersion ip_version;
  Http::CodecType upstream_protocol;
};

class HttpHealthCheckIntegrationTestBase
    : public testing::TestWithParam<HttpHealthCheckIntegrationTestParams>,
      public HealthCheckIntegrationTestBase {
public:
  HttpHealthCheckIntegrationTestBase()
      : HealthCheckIntegrationTestBase(GetParam().ip_version, GetParam().upstream_protocol) {}

  // Returns the 4 combinations for testing:
  // [HTTP1, HTTP2] x [IPv4, IPv6]
  static std::vector<HttpHealthCheckIntegrationTestParams>
  getHttpHealthCheckIntegrationTestParams() {
    std::vector<HttpHealthCheckIntegrationTestParams> ret;

    for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
      for (auto upstream_protocol : {Http::CodecType::HTTP1, Http::CodecType::HTTP2}) {
        ret.push_back(HttpHealthCheckIntegrationTestParams{ip_version, upstream_protocol});
      }
    }
    return ret;
  }

  static std::string httpHealthCheckTestParamsToString(
      const ::testing::TestParamInfo<HttpHealthCheckIntegrationTestParams>& params) {
    return absl::StrCat(
        (params.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
        (params.param.upstream_protocol == Http::CodecType::HTTP2 ? "Http2Upstream"
                                                                  : "HttpUpstream"));
  }

  void TearDown() override {
    cleanupHostConnections();
    cleanUpXdsConnection();
  }

  // Adds a HTTP active health check specifier to the given cluster, and waits for the first health
  // check probe to be received.
  void initHttpHealthCheck(uint32_t cluster_idx, int unhealthy_threshold = 1,
                           std::unique_ptr<envoy::type::v3::Int64Range> retriable_range = nullptr) {
    const envoy::type::v3::CodecClientType codec_client_type =
        (Http::CodecType::HTTP1 == upstream_protocol_) ? envoy::type::v3::CodecClientType::HTTP1
                                                       : envoy::type::v3::CodecClientType::HTTP2;

    auto& cluster_data = clusters_[cluster_idx];
    auto* health_check = addHealthCheck(cluster_data.cluster_);
    health_check->mutable_http_health_check()->set_path("/healthcheck");
    health_check->mutable_http_health_check()->set_codec_client_type(codec_client_type);
    health_check->mutable_unhealthy_threshold()->set_value(unhealthy_threshold);
    if (retriable_range != nullptr) {
      auto* range = health_check->mutable_http_health_check()->add_retriable_statuses();
      range->set_start(retriable_range->start());
      range->set_end(retriable_range->end());
    }

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

class HttpHealthCheckIntegrationTest : public Event::TestUsingSimulatedTime,
                                       public HttpHealthCheckIntegrationTestBase {};

INSTANTIATE_TEST_SUITE_P(
    IpHttpVersions, HttpHealthCheckIntegrationTest,
    testing::ValuesIn(HttpHealthCheckIntegrationTest::getHttpHealthCheckIntegrationTestParams()),
    HttpHealthCheckIntegrationTest::httpHealthCheckTestParamsToString);

class RealTimeHttpHealthCheckIntegrationTest : public HttpHealthCheckIntegrationTestBase {};

INSTANTIATE_TEST_SUITE_P(
    IpHttpVersions, RealTimeHttpHealthCheckIntegrationTest,
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

// Tests that a retriable status response does not mark endpoint unhealthy until threshold is
// reached
TEST_P(HttpHealthCheckIntegrationTest, SingleEndpointUnhealthyThresholdHttp) {
  const uint32_t cluster_idx = 0;
  initialize();
  auto retriable_range = std::make_unique<envoy::type::v3::Int64Range>();
  retriable_range->set_start(400);
  retriable_range->set_end(401);
  initHttpHealthCheck(cluster_idx, 2, std::move(retriable_range));

  // Responds with healthy status.
  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  clusters_[cluster_idx].host_stream_->encodeData(0, true);

  // Wait for health check
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.attempt", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
  test_server_->waitForGaugeEq("cluster.cluster_1.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_total")->value());

  // Wait until the next attempt is made.
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.attempt", 2);

  // Respond with retriable status
  ASSERT_TRUE(clusters_[cluster_idx].host_fake_connection_->waitForNewStream(
      *dispatcher_, clusters_[cluster_idx].host_stream_));
  ASSERT_TRUE(clusters_[cluster_idx].host_stream_->waitForEndStream(*dispatcher_));

  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getPathValue(), "/healthcheck");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getMethodValue(), "GET");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getHostValue(),
            clusters_[cluster_idx].name_);
  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "400"}}, false);
  clusters_[cluster_idx].host_stream_->encodeData(0, true);

  // Wait for second health check
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_healthy")->value());

  // Wait until the next attempt is made.
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.attempt", 3);

  // Respond with retriable status a second time, matching unhealthy threshold
  ASSERT_TRUE(clusters_[cluster_idx].host_fake_connection_->waitForNewStream(
      *dispatcher_, clusters_[cluster_idx].host_stream_));
  ASSERT_TRUE(clusters_[cluster_idx].host_stream_->waitForEndStream(*dispatcher_));

  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getPathValue(), "/healthcheck");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getMethodValue(), "GET");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getHostValue(),
            clusters_[cluster_idx].name_);
  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "400"}}, false);
  clusters_[cluster_idx].host_stream_->encodeData(0, true);

  // Wait for third health check
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.failure", 2);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  test_server_->waitForGaugeEq("cluster.cluster_1.membership_healthy", 0);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_total")->value());

  // Wait until the next attempt is made.
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.attempt", 4);

  // Respond with healthy status again.
  ASSERT_TRUE(clusters_[cluster_idx].host_fake_connection_->waitForNewStream(
      *dispatcher_, clusters_[cluster_idx].host_stream_));
  ASSERT_TRUE(clusters_[cluster_idx].host_stream_->waitForEndStream(*dispatcher_));

  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getPathValue(), "/healthcheck");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getMethodValue(), "GET");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getHostValue(),
            clusters_[cluster_idx].name_);
  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  clusters_[cluster_idx].host_stream_->encodeData(0, true);

  // Wait for fourth health check
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.success", 2);
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
  test_server_->waitForGaugeEq("cluster.cluster_1.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_total")->value());
}

// Tests that expected statuses takes precedence over retriable statuses
TEST_P(HttpHealthCheckIntegrationTest, SingleEndpointExpectedAndRetriablePrecedence) {
  const uint32_t cluster_idx = 0;
  initialize();
  auto retriable_range = std::make_unique<envoy::type::v3::Int64Range>();
  retriable_range->set_start(200);
  retriable_range->set_end(201);
  initHttpHealthCheck(cluster_idx, 2, std::move(retriable_range));

  // Responds with healthy status.
  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  clusters_[cluster_idx].host_stream_->encodeData(0, true);

  // Wait for health check
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.attempt", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
  test_server_->waitForGaugeEq("cluster.cluster_1.membership_healthy", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_total")->value());
}

// Verify that immediate health check fail causes cluster exclusion.
TEST_P(HttpHealthCheckIntegrationTest, SingleEndpointImmediateHealthcheckFailHttp) {
  const uint32_t cluster_idx = 0;
  initialize();
  initHttpHealthCheck(cluster_idx);

  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_1.membership_excluded")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_1.membership_healthy")->value());

  // Endpoint responds to the health check with unhealthy status and immediate health check failure.
  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "503"},
                                      {"x-envoy-immediate-health-check-fail", "true"}},
      false);
  clusters_[cluster_idx].host_stream_->encodeData(1024, true);

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.attempt")->value());
  test_server_->waitForGaugeEq("cluster.cluster_1.membership_excluded", 1);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_1.membership_healthy")->value());

  // Wait until the next attempt is made.
  test_server_->waitForCounterEq("cluster.cluster_1.health_check.attempt", 2);

  ASSERT_TRUE(clusters_[cluster_idx].host_fake_connection_->waitForNewStream(
      *dispatcher_, clusters_[cluster_idx].host_stream_));
  ASSERT_TRUE(clusters_[cluster_idx].host_stream_->waitForEndStream(*dispatcher_));

  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getPathValue(), "/healthcheck");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getMethodValue(), "GET");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getHostValue(),
            clusters_[cluster_idx].name_);

  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
  test_server_->waitForGaugeEq("cluster.cluster_1.membership_excluded", 0);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_1.membership_healthy")->value());
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

// Tests that health checking gracefully handles a NO_ERROR GOAWAY from the upstream.
TEST_P(HttpHealthCheckIntegrationTest, SingleEndpointGoAway) {
  initialize();

  // GOAWAY doesn't exist in HTTP1.
  if (upstream_protocol_ == Http::CodecType::HTTP1) {
    return;
  }

  const uint32_t cluster_idx = 0;
  initHttpHealthCheck(cluster_idx);

  // Send a GOAWAY with NO_ERROR and then a 200. The health checker should allow the request
  // to finish despite the GOAWAY.
  clusters_[cluster_idx].host_fake_connection_->encodeGoAway();
  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
  ASSERT_TRUE(clusters_[cluster_idx].host_fake_connection_->waitForDisconnect());

  // Advance time to cause another health check.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(500));

  ASSERT_TRUE(clusters_[cluster_idx].host_upstream_->waitForHttpConnection(
      *dispatcher_, clusters_[cluster_idx].host_fake_connection_));
  ASSERT_TRUE(clusters_[cluster_idx].host_fake_connection_->waitForNewStream(
      *dispatcher_, clusters_[cluster_idx].host_stream_));
  ASSERT_TRUE(clusters_[cluster_idx].host_stream_->waitForEndStream(*dispatcher_));

  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getPathValue(), "/healthcheck");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getMethodValue(), "GET");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getHostValue(),
            clusters_[cluster_idx].name_);

  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 2);
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

// Tests that health checking properly handles a GOAWAY with an error, followed
// by a reset. This test uses the real time system because it flakes with
// simulated time.
// The test goes through this sequence:
// 1) send a GOAWAY with a PROTOCOL_ERROR code from the upstream
// 2) waitForDisconnect on the health check connection
// 3) advance time to trigger another health check
// 4) wait for a new health check on a new connection.
//
// The flake was caused by the GOAWAY simultaneously causing the downstream
// health checker to close the connection (because of the GOAWAY) and the fake
// upstream to also close the connection (because of special handling for
// protocol errors). This meant that waitForDisconnect() could finish waiting
// before the health checker saw the GOAWAY and enabled the health check
// interval timer. This would cause simulated time to advance too early, and no
// followup health check would happen. Using real time solves this because then
// the ordering of advancing the time system and enabling the health check timer
// is inconsequential.
TEST_P(RealTimeHttpHealthCheckIntegrationTest, SingleEndpointGoAwayError) {
  initialize();

  // GOAWAY doesn't exist in HTTP1.
  if (upstream_protocol_ == Http::CodecType::HTTP1) {
    return;
  }

  const uint32_t cluster_idx = 0;
  initHttpHealthCheck(cluster_idx);

  // Send a GOAWAY with an error. The health checker should treat this as an
  // error and cancel the request.
  clusters_[cluster_idx].host_fake_connection_->encodeProtocolError();

  ASSERT_TRUE(clusters_[cluster_idx].host_fake_connection_->waitForDisconnect());
  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());

  // Advance time to cause another health check.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(500));

  ASSERT_TRUE(clusters_[cluster_idx].host_upstream_->waitForHttpConnection(
      *dispatcher_, clusters_[cluster_idx].host_fake_connection_));
  ASSERT_TRUE(clusters_[cluster_idx].host_fake_connection_->waitForNewStream(
      *dispatcher_, clusters_[cluster_idx].host_stream_));
  ASSERT_TRUE(clusters_[cluster_idx].host_stream_->waitForEndStream(*dispatcher_));

  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getPathValue(), "/healthcheck");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getMethodValue(), "GET");
  EXPECT_EQ(clusters_[cluster_idx].host_stream_->headers().getHostValue(),
            clusters_[cluster_idx].name_);

  clusters_[cluster_idx].host_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

class TcpHealthCheckIntegrationTest : public Event::TestUsingSimulatedTime,
                                      public testing::TestWithParam<Network::Address::IpVersion>,
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

  void
  initProxyProtoHealthCheck(uint32_t cluster_idx,
                            envoy::config::core::v3::ProxyProtocolConfig proxy_protocol_config) {
    auto& cluster_data = clusters_[cluster_idx];
    auto health_check = addHealthCheck(cluster_data.cluster_);
    health_check->mutable_tcp_health_check()->mutable_send()->set_text("50696E67"); // "Ping"
    health_check->mutable_tcp_health_check()->add_receive()->set_text("506F6E67");  // "Pong"
    health_check->mutable_tcp_health_check()->mutable_proxy_protocol_config()->CopyFrom(
        proxy_protocol_config);

    // Introduce the cluster using compareDiscoveryRequest / sendDiscoveryResponse.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {cluster_data.cluster_}, {cluster_data.cluster_}, {}, "55");

    // Wait for upstream to receive TCP HC request.
    ASSERT_TRUE(
        cluster_data.host_upstream_->waitForRawConnection(cluster_data.host_fake_raw_connection_));
    if (proxy_protocol_config.version() ==
        envoy::config::core::v3::ProxyProtocolConfig_Version_V1) {
      ASSERT_TRUE(
          cluster_data.host_fake_raw_connection_->waitForData([](const std::string& data) -> bool {
            if (GetParam() == Network::Address::IpVersion::v4) {
              return data.find("Ping") != std::string::npos &&
                     data.find("PROXY TCP4 127.0.0.1 127.0.0.1") != std::string::npos;
            }
            return data.find("Ping") != std::string::npos &&
                   data.find("PROXY TCP6 ::1 ::1") != std::string::npos;
          }));
    } else {
      // ProxyProtocol Signature + Local Command + "Ping"
      const char header[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                             0x54, 0x0a, 0x20, 0x00, 0x00, 0x00, 0x50, 0x69, 0x6e, 0x67};
      ASSERT_TRUE(cluster_data.host_fake_raw_connection_->waitForData(
          FakeRawConnection::waitForInexactMatch(std::string(header).c_str())));
    }
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

// Tests that a healthy endpoint returns a valid TCP health check response with ProxyProtocol.
TEST_P(TcpHealthCheckIntegrationTest, SingleEndpointHealthyTcpWithProxyProtocolV1) {
  envoy::config::core::v3::ProxyProtocolConfig proxy_protocol_config;
  proxy_protocol_config.set_version(envoy::config::core::v3::ProxyProtocolConfig_Version_V1);

  const uint32_t cluster_idx = 0;
  initialize();
  initProxyProtoHealthCheck(cluster_idx, proxy_protocol_config);

  AssertionResult result = clusters_[cluster_idx].host_fake_raw_connection_->write("Pong");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

TEST_P(TcpHealthCheckIntegrationTest, SingleEndpointHealthyTcpWithProxyProtocolV2) {
  envoy::config::core::v3::ProxyProtocolConfig proxy_protocol_config;
  proxy_protocol_config.set_version(envoy::config::core::v3::ProxyProtocolConfig_Version_V2);

  const uint32_t cluster_idx = 0;
  initialize();
  initProxyProtoHealthCheck(cluster_idx, proxy_protocol_config);

  AssertionResult result = clusters_[cluster_idx].host_fake_raw_connection_->write("Pong");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
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

class GrpcHealthCheckIntegrationTest : public Event::TestUsingSimulatedTime,
                                       public testing::TestWithParam<Network::Address::IpVersion>,
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
    clusters_[cluster_idx].host_stream_->startGrpcStream(false);
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

class ExternalHealthCheckIntegrationTest
    : public Event::TestUsingSimulatedTime,
      public testing::TestWithParam<Network::Address::IpVersion>,
      public HealthCheckIntegrationTestBase {
public:
  ExternalHealthCheckIntegrationTest() : HealthCheckIntegrationTestBase(GetParam()) {}

  void TearDown() override {
    cleanupHostConnections();
    cleanUpXdsConnection();
  }

  // Adds a EXTERNAL active health check specifier to the given cluster, and waits for the first
  // health check probe to be received.
  void initExternalHealthCheck(uint32_t cluster_idx) {
    auto& cluster_data = clusters_[cluster_idx];
    auto& cluster = cluster_data.cluster_;
    auto health_check = addHealthCheck(cluster_data.cluster_);
    auto* socket_address = cluster.mutable_load_assignment()
                               ->mutable_endpoints(0)
                               ->mutable_lb_endpoints(0)
                               ->mutable_endpoint()
                               ->mutable_health_check_config()
                               ->mutable_address()
                               ->mutable_socket_address();

    health_check->mutable_tcp_health_check()->mutable_send()->set_text("50696E67"); // "Ping"
    health_check->mutable_tcp_health_check()->add_receive()->set_text("506F6E67");  // "Pong"

    socket_address->set_address(Network::Test::getLoopbackAddressString(ip_version_));
    socket_address->set_port_value(
        cluster_data.external_host_upstream_->localAddress()->ip()->port());

    // Introduce the cluster using compareDiscoveryRequest / sendDiscoveryResponse.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {cluster_data.cluster_}, {cluster_data.cluster_}, {}, "55");

    // Wait for upstream to receive EXTERNAL HC request.
    ASSERT_TRUE(cluster_data.external_host_upstream_->waitForRawConnection(
        cluster_data.external_host_fake_raw_connection_));
    ASSERT_TRUE(cluster_data.external_host_fake_raw_connection_->waitForData(
        FakeRawConnection::waitForInexactMatch("Ping")));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ExternalHealthCheckIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Tests that a healthy endpoint returns a valid EXTERNAL health check response.
TEST_P(ExternalHealthCheckIntegrationTest, SingleEndpointHealthyExternal) {
  const uint32_t cluster_idx = 0;
  initialize();
  initExternalHealthCheck(cluster_idx);

  AssertionResult result = clusters_[cluster_idx].external_host_fake_raw_connection_->write("Pong");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.success", 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

// Tests that an invalid response fails the health check.
TEST_P(ExternalHealthCheckIntegrationTest, SingleEndpointWrongResponseExternal) {
  const uint32_t cluster_idx = 0;
  initialize();
  initExternalHealthCheck(cluster_idx);

  // Send the wrong reply ("Pong" is expected).
  AssertionResult result =
      clusters_[cluster_idx].external_host_fake_raw_connection_->write("Poong");
  RELEASE_ASSERT(result, result.message());

  // Envoy will wait until timeout occurs because no correct reply was received.
  // Increase time until timeout (30s).
  timeSystem().advanceTimeWait(std::chrono::seconds(30));

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

// Tests that no EXTERNAL health check response results in timeout and unhealthy endpoint.
TEST_P(ExternalHealthCheckIntegrationTest, SingleEndpointTimeoutExternal) {
  const uint32_t cluster_idx = 0;
  initialize();
  initExternalHealthCheck(cluster_idx);

  // Increase time until timeout (30s).
  timeSystem().advanceTimeWait(std::chrono::seconds(30));

  test_server_->waitForCounterGe("cluster.cluster_1.health_check.failure", 1);
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

} // namespace
} // namespace Envoy
