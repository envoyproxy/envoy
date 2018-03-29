#include "envoy/api/v2/eds.pb.h"

#include "common/config/resources.h"

#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Integration test for EDS features. EDS is consumed via filesystem
// subscription.
class EdsIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  EdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  // We need to supply the endpoints via EDS to provide health status. Use a
  // filesystem delivery to simplify test mechanics.
  void updateEndpoints(uint32_t total_endpoints, uint32_t healthy_endpoints) {
    envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("cluster_0");
    auto* locality_lb_endpoints = cluster_load_assignment.add_endpoints();

    for (uint32_t i = 0; i < total_endpoints; ++i) {
      auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
      auto* socket_address =
          endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
      socket_address->set_address(Network::Test::getLoopbackAddressString(GetParam()));
      socket_address->set_port_value(fake_upstreams_[i]->localAddress()->ip()->port());
      if (i >= healthy_endpoints) {
        endpoint->set_health_status(envoy::api::v2::core::HealthStatus::UNHEALTHY);
      }
    }

    // Write to file the DiscoveryResponse and trigger inotify watch.
    envoy::api::v2::DiscoveryResponse eds_response;
    eds_response.set_version_info(std::to_string(eds_version_++));
    eds_response.set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);
    eds_response.add_resources()->PackFrom(cluster_load_assignment);
    // Past the initial write, need move semantics to trigger inotify move event that the
    // FilesystemSubscriptionImpl is subscribed to.
    if (eds_path_.empty()) {
      eds_path_ =
          TestEnvironment::writeStringToFileForTest("eds.pb_text", eds_response.DebugString());
    } else {
      std::string path = TestEnvironment::writeStringToFileForTest("eds.update.pb_text",
                                                                   eds_response.DebugString());
      ASSERT_EQ(0, ::rename(path.c_str(), eds_path_.c_str()));
    }
  }

  void createUpstreams() override {}

  void initialize() override {
    updateEndpoints(0, 0);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to EDS filesystem sourcing.
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster_0->mutable_hosts()->Clear();
      cluster_0->set_type(envoy::api::v2::Cluster::EDS);
      auto* eds_cluster_config = cluster_0->mutable_eds_cluster_config();
      eds_cluster_config->mutable_eds_config()->set_path(eds_path_);
    });
    HttpIntegrationTest::initialize();
    for (uint32_t i = 0; i < upstream_endpoints_; ++i) {
      fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    }
  }

  static constexpr uint32_t upstream_endpoints_ = 5;
  std::string eds_path_;
  uint32_t eds_version_{};
};

INSTANTIATE_TEST_CASE_P(IpVersions, EdsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Validate that health status updates are consumed from EDS.
TEST_P(EdsIntegrationTest, HealthUpdate) {
  initialize();
  // Initial state, no cluster members.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // 2/2 healthy endpoints.
  updateEndpoints(2, 2);
  test_server_->waitForCounterGe("cluster.cluster_0.update_success", 2);
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Drop to 0/2 healthy endpoints.
  updateEndpoints(2, 0);
  test_server_->waitForCounterGe("cluster.cluster_0.update_success", 3);
  EXPECT_EQ(3, test_server_->counter("cluster.cluster_0.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Increase to 1/2 healthy endpoints.
  updateEndpoints(2, 1);
  test_server_->waitForCounterGe("cluster.cluster_0.update_success", 4);
  EXPECT_EQ(4, test_server_->counter("cluster.cluster_0.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Add host and modify health to 2/3 healthy endpoints.
  updateEndpoints(3, 2);
  test_server_->waitForCounterGe("cluster.cluster_0.update_success", 5);
  EXPECT_EQ(5, test_server_->counter("cluster.cluster_0.update_success")->value());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

} // namespace
} // namespace Envoy
