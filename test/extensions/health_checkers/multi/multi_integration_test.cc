#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/health_checkers/multi/v3/multi.pb.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

using testing::Eq;
using testing::Ge;

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace Multi {
namespace {

class MultiHealthCheckIntegrationTest : public Event::TestUsingSimulatedTime,
                                        public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  MultiHealthCheckIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam(), ConfigHelper::httpProxyConfig()) {}

  void TearDown() override {
    for (auto& conn : hc_connections_) {
      if (conn != nullptr) {
        AssertionResult result = conn->close();
        RELEASE_ASSERT(result, result.message());
      }
    }
  }

  void addMultiTcpHealthCheck(envoy::config::cluster::v3::Cluster* cluster,
                              const std::string& name1 = "", const std::string& name2 = "") {
    auto* health_check = cluster->add_health_checks();
    health_check->mutable_timeout()->set_seconds(30);
    health_check->mutable_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_no_traffic_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    health_check->mutable_unhealthy_threshold()->set_value(1);
    health_check->mutable_healthy_threshold()->set_value(1);

    auto* custom = health_check->mutable_custom_health_check();
    custom->set_name("envoy.health_checkers.multi");

    envoy::extensions::health_checkers::multi::v3::Multi multi_config;

    auto* sub1 = multi_config.add_health_checks();
    if (!name1.empty()) {
      sub1->set_name(name1);
    }
    auto* hc1 = sub1->mutable_health_check();
    hc1->mutable_timeout()->set_seconds(30);
    hc1->mutable_interval()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    hc1->mutable_no_traffic_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    hc1->mutable_unhealthy_threshold()->set_value(1);
    hc1->mutable_healthy_threshold()->set_value(1);
    hc1->mutable_tcp_health_check()->mutable_send()->set_text("50696E6731");
    hc1->mutable_tcp_health_check()->add_receive()->set_text("506F6E6731");

    auto* sub2 = multi_config.add_health_checks();
    if (!name2.empty()) {
      sub2->set_name(name2);
    }
    auto* hc2 = sub2->mutable_health_check();
    hc2->mutable_timeout()->set_seconds(30);
    hc2->mutable_interval()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    hc2->mutable_no_traffic_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    hc2->mutable_unhealthy_threshold()->set_value(1);
    hc2->mutable_healthy_threshold()->set_value(1);
    hc2->mutable_tcp_health_check()->mutable_send()->set_text("50696E6732");
    hc2->mutable_tcp_health_check()->add_receive()->set_text("506F6E6732");

    std::ignore = custom->mutable_typed_config()->PackFrom(multi_config);
  }

  void initializeWithStaticCluster(const std::string& name1 = "", const std::string& name2 = "") {
    use_lds_ = false;
    defer_listener_finalization_ = true;

    auto up_config = upstreamConfig();
    up_config.upstream_protocol_ = Http::CodecType::HTTP1;
    host_upstream_ = std::make_unique<FakeUpstream>(0, version_, up_config);

    config_helper_.addConfigModifier(
        [this, name1, name2](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
          cluster->set_name("cluster_1");
          cluster->mutable_connect_timeout()->set_seconds(5);

          auto* load_assignment = cluster->mutable_load_assignment();
          load_assignment->set_cluster_name("cluster_1");
          auto* ep = load_assignment->add_endpoints()->add_lb_endpoints()->mutable_endpoint();
          ep->mutable_address()->mutable_socket_address()->set_address(
              Network::Test::getLoopbackAddressString(GetParam()));
          ep->mutable_address()->mutable_socket_address()->set_port_value(
              host_upstream_->localAddress()->ip()->port());

          addMultiTcpHealthCheck(cluster, name1, name2);
        });

    HttpIntegrationTest::initialize();

    ASSERT_TRUE(host_upstream_->waitForRawConnection(hc_connections_.emplace_back()));
    ASSERT_TRUE(host_upstream_->waitForRawConnection(hc_connections_.emplace_back()));
    ASSERT_TRUE(hc_connections_[0]->waitForData(FakeRawConnection::waitForInexactMatch("Ping")));
    ASSERT_TRUE(hc_connections_[1]->waitForData(FakeRawConnection::waitForInexactMatch("Ping")));
  }

  void initializeWithEds() {
    use_lds_ = false;
    defer_listener_finalization_ = true;

    auto up_config = upstreamConfig();
    up_config.upstream_protocol_ = Http::CodecType::HTTP1;
    host_upstream_ = std::make_unique<FakeUpstream>(0, version_, up_config);

    auto cla = ConfigHelper::buildClusterLoadAssignment(
        "cluster_1", Network::Test::getLoopbackAddressString(GetParam()),
        host_upstream_->localAddress()->ip()->port());
    eds_helper_.setEds({cla});

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->set_name("cluster_1");
      cluster->set_type(envoy::config::cluster::v3::Cluster::EDS);
      cluster->mutable_connect_timeout()->set_seconds(5);
      cluster->mutable_eds_cluster_config()
          ->mutable_eds_config()
          ->mutable_path_config_source()
          ->set_path(eds_helper_.edsPath());

      addMultiTcpHealthCheck(cluster);
    });

    HttpIntegrationTest::initialize();

    ASSERT_TRUE(host_upstream_->waitForRawConnection(hc_connections_.emplace_back()));
    ASSERT_TRUE(host_upstream_->waitForRawConnection(hc_connections_.emplace_back()));
    ASSERT_TRUE(hc_connections_[0]->waitForData(FakeRawConnection::waitForInexactMatch("Ping")));
    ASSERT_TRUE(hc_connections_[1]->waitForData(FakeRawConnection::waitForInexactMatch("Ping")));
  }

  EdsHelper eds_helper_;
  FakeUpstreamPtr host_upstream_;
  FakeUpstreamPtr host_upstream_2_;
  std::vector<FakeRawConnectionPtr> hc_connections_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MultiHealthCheckIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Both sub-checkers pass: host becomes healthy.
TEST_P(MultiHealthCheckIntegrationTest, BothSubCheckersPass) {
  initializeWithStaticCluster();

  AssertionResult result = hc_connections_[0]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());
  result = hc_connections_[1]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForGauge("cluster.cluster_1.membership_healthy", Eq(1));
  test_server_->waitForGauge("cluster.cluster_1.membership_total", Eq(1));
}

// One sub-checker times out: host stays unhealthy.
TEST_P(MultiHealthCheckIntegrationTest, OneSubCheckerTimeout) {
  initializeWithStaticCluster();

  // Respond to only one connection. The other will time out.
  AssertionResult result = hc_connections_[0]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());

  timeSystem().advanceTimeWait(std::chrono::seconds(30));

  test_server_->waitForCounter("cluster.cluster_1.health_check.failure", Ge(1));
  test_server_->waitForGauge("cluster.cluster_1.membership_healthy", Eq(0));
}

// Without names, both sub-checkers share the cluster stats scope.
TEST_P(MultiHealthCheckIntegrationTest, StatsWithoutName) {
  initializeWithStaticCluster();

  AssertionResult result = hc_connections_[0]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());
  result = hc_connections_[1]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForGauge("cluster.cluster_1.membership_healthy", Eq(1));

  test_server_->waitForCounter("cluster.cluster_1.health_check.attempt", Ge(2));
  test_server_->waitForCounter("cluster.cluster_1.health_check.success", Ge(2));
  test_server_->waitForCounter("cluster.cluster_1.health_check.failure", Eq(0));
}

// With names, each sub-checker gets its own stats scope.
TEST_P(MultiHealthCheckIntegrationTest, StatsWithName) {
  initializeWithStaticCluster("first", "second");

  AssertionResult result = hc_connections_[0]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());
  result = hc_connections_[1]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForGauge("cluster.cluster_1.membership_healthy", Eq(1));

  test_server_->waitForCounter("cluster.cluster_1.health_check.name.first.health_check.attempt",
                               Ge(1));
  test_server_->waitForCounter("cluster.cluster_1.health_check.name.second.health_check.attempt",
                               Ge(1));
}

// Adding a host via EDS exercises onClusterMemberUpdate (add path).
TEST_P(MultiHealthCheckIntegrationTest, HostAddAfterStart) {
  initializeWithEds();

  AssertionResult result = hc_connections_[0]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());
  result = hc_connections_[1]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForGauge("cluster.cluster_1.membership_healthy", Eq(1));

  auto config = upstreamConfig();
  config.upstream_protocol_ = Http::CodecType::HTTP1;
  host_upstream_2_ = std::make_unique<FakeUpstream>(0, version_, config);

  // Update EDS with both endpoints.
  auto cla = ConfigHelper::buildClusterLoadAssignment(
      "cluster_1", Network::Test::getLoopbackAddressString(GetParam()),
      host_upstream_->localAddress()->ip()->port());
  auto* ep2 = cla.mutable_endpoints(0)->add_lb_endpoints()->mutable_endpoint();
  ep2->mutable_address()->mutable_socket_address()->set_address(
      Network::Test::getLoopbackAddressString(GetParam()));
  ep2->mutable_address()->mutable_socket_address()->set_port_value(
      host_upstream_2_->localAddress()->ip()->port());
  eds_helper_.setEds({cla});

  ASSERT_TRUE(host_upstream_2_->waitForRawConnection(hc_connections_.emplace_back()));
  ASSERT_TRUE(host_upstream_2_->waitForRawConnection(hc_connections_.emplace_back()));
  ASSERT_TRUE(hc_connections_[2]->waitForData(FakeRawConnection::waitForInexactMatch("Ping")));
  ASSERT_TRUE(hc_connections_[3]->waitForData(FakeRawConnection::waitForInexactMatch("Ping")));

  result = hc_connections_[2]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());
  result = hc_connections_[3]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForGauge("cluster.cluster_1.membership_healthy", Eq(2));
  test_server_->waitForGauge("cluster.cluster_1.membership_total", Eq(2));
}

// Removing a host via EDS exercises onClusterMemberUpdate (remove path).
TEST_P(MultiHealthCheckIntegrationTest, HostRemoveAfterStart) {
  initializeWithEds();

  AssertionResult result = hc_connections_[0]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());
  result = hc_connections_[1]->write("Pong1Pong2");
  RELEASE_ASSERT(result, result.message());

  test_server_->waitForGauge("cluster.cluster_1.membership_healthy", Eq(1));

  // Remove the endpoint via EDS.
  envoy::config::endpoint::v3::ClusterLoadAssignment empty_cla;
  empty_cla.set_cluster_name("cluster_1");
  eds_helper_.setEds({empty_cla});

  test_server_->waitForGauge("cluster.cluster_1.membership_total", Eq(0));
}

} // namespace
} // namespace Multi
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
