#include "envoy/api/v2/eds.pb.h"

#include "common/upstream/load_balancer_impl.h"

#include "test/config/utility.h"
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
  void setEndpoints(uint32_t total_endpoints, uint32_t healthy_endpoints,
                    absl::optional<uint32_t> overprovisioning_factor = absl::nullopt) {
    envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("cluster_0");
    if (overprovisioning_factor.has_value()) {
      cluster_load_assignment.mutable_policy()->mutable_overprovisioning_factor()->set_value(
          overprovisioning_factor.value());
    }
    auto* locality_lb_endpoints = cluster_load_assignment.add_endpoints();

    for (uint32_t i = 0; i < total_endpoints; ++i) {
      auto* endpoint = locality_lb_endpoints->add_lb_endpoints();
      setUpstreamAddress(i, *endpoint);
      if (i >= healthy_endpoints) {
        endpoint->set_health_status(envoy::api::v2::core::HealthStatus::UNHEALTHY);
      }
    }
    eds_helper_.setEds({cluster_load_assignment}, *test_server_);
  }

  void initialize() override {
    setUpstreamCount(4);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to EDS filesystem sourcing.
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster_0->mutable_hosts()->Clear();
      cluster_0->set_type(envoy::api::v2::Cluster::EDS);
      auto* eds_cluster_config = cluster_0->mutable_eds_cluster_config();
      eds_cluster_config->mutable_eds_config()->set_path(eds_helper_.eds_path());
    });
    HttpIntegrationTest::initialize();
    setEndpoints(0, 0);
  }

  EdsHelper eds_helper_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, EdsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Validate that health status updates are consumed from EDS.
TEST_P(EdsIntegrationTest, HealthUpdate) {
  initialize();
  // Initial state, no cluster members.
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // 2/2 healthy endpoints.
  setEndpoints(2, 2);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Drop to 0/2 healthy endpoints.
  setEndpoints(2, 0);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(0, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Increase to 1/2 healthy endpoints.
  setEndpoints(2, 1);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
  // Add host and modify health to 2/3 healthy endpoints.
  setEndpoints(3, 2);
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.membership_change")->value());
  EXPECT_EQ(3, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(2, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// Validate that overprovisioning_factor update are picked up by Envoy.
TEST_P(EdsIntegrationTest, OverprovisioningFactorUpdate) {
  initialize();
  // Default overprovisioning factor.
  setEndpoints(4, 4);
  auto get_and_compare = [this](const uint32_t expected_factor) {
    const auto& cluster_map = test_server_->server().clusterManager().clusters();
    EXPECT_EQ(1, cluster_map.size());
    EXPECT_EQ(1, cluster_map.count("cluster_0"));
    const auto& cluster_ref = cluster_map.find("cluster_0")->second;
    const auto& hostset_per_priority = cluster_ref.get().prioritySet().hostSetsPerPriority();
    EXPECT_EQ(1, hostset_per_priority.size());
    const Envoy::Upstream::HostSetPtr& host_set = hostset_per_priority[0];
    EXPECT_EQ(expected_factor, host_set->overprovisioning_factor());
  };
  get_and_compare(Envoy::Upstream::kDefaultOverProvisioningFactor);

  // Use new overprovisioning factor 200.
  setEndpoints(4, 4, 200);
  get_and_compare(200);
}

} // namespace
} // namespace Envoy
