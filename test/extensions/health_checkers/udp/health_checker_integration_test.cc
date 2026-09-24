#include <memory>

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/health_checkers/udp/v3/udp.pb.h"

#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

using testing::Ge;

namespace Envoy {
namespace {

class UdpHealthCheckerIntegrationTest : public Event::TestUsingSimulatedTime,
                                        public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  UdpHealthCheckerIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam(),
                            ConfigHelper::discoveredClustersBootstrap("GRPC")) {}

  void initialize() override {
    use_lds_ = false;
    setUpstreamCount(1);
    setUpstreamProtocol(Http::CodecType::HTTP2);
    defer_listener_finalization_ = true;
    HttpIntegrationTest::initialize();

    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();

    test_server_->waitForGauge("cluster_manager.active_clusters", Ge(1));
    registerTestServerPorts({"http"});

    auto config = upstreamConfig();
    config.udp_fake_upstream_.emplace();
    host_upstream_ = std::make_unique<FakeUpstream>(0, version_, config);
    cluster_ =
        ConfigHelper::buildStaticCluster("cluster_1", host_upstream_->localAddress()->ip()->port(),
                                         Network::Test::getLoopbackAddressString(GetParam()));
  }

  void TearDown() override { cleanUpXdsConnection(); }

  void initHealthCheck(Network::UdpRecvData& request) {
    auto* health_check = cluster_.add_health_checks();
    health_check->mutable_timeout()->set_seconds(30);
    health_check->mutable_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_no_traffic_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_unhealthy_threshold()->set_value(1);
    health_check->mutable_healthy_threshold()->set_value(1);

    auto* custom_health_check = health_check->mutable_custom_health_check();
    custom_health_check->set_name("envoy.health_checkers.udp");
    envoy::extensions::health_checkers::udp::v3::UdpHealthCheck udp_config;
    udp_config.mutable_send()->set_binary("Ping");
    udp_config.mutable_receive()->set_binary("Pong");
    ASSERT_TRUE(custom_health_check->mutable_typed_config()->PackFrom(udp_config));

    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                               {cluster_}, {cluster_}, {}, "55");

    ASSERT_TRUE(host_upstream_->waitForUdpDatagram(request));
    EXPECT_EQ("Ping", request.buffer_->toString());
  }

  FakeUpstreamPtr host_upstream_;
  envoy::config::cluster::v3::Cluster cluster_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpHealthCheckerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UdpHealthCheckerIntegrationTest, HealthyResponse) {
  initialize();

  Network::UdpRecvData request;
  initHealthCheck(request);
  host_upstream_->sendUdpDatagram("Pong", request.addresses_.peer_);

  test_server_->waitForCounter("cluster.cluster_1.health_check.success", Ge(1));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

TEST_P(UdpHealthCheckerIntegrationTest, MismatchThenMatchingResponse) {
  initialize();

  Network::UdpRecvData request;
  initHealthCheck(request);
  host_upstream_->sendUdpDatagram("Poong", request.addresses_.peer_);
  host_upstream_->sendUdpDatagram("Pong", request.addresses_.peer_);

  test_server_->waitForCounter("cluster.cluster_1.health_check.success", Ge(1));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

TEST_P(UdpHealthCheckerIntegrationTest, Timeout) {
  initialize();

  Network::UdpRecvData request;
  initHealthCheck(request);
  timeSystem().advanceTimeWait(std::chrono::seconds(30));

  test_server_->waitForCounter("cluster.cluster_1.health_check.failure", Ge(1));
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.health_check.success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.health_check.failure")->value());
}

} // namespace
} // namespace Envoy
