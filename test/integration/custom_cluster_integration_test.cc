#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"

#include "test/config/utility.h"
#include "test/integration/clusters/cluster_factory_config.pb.h"
#include "test/integration/clusters/custom_static_cluster.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

const int UpstreamIndex = 0;

// Integration test for cluster extension using CustomStaticCluster.
class CustomClusterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  CustomClusterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    setUpstreamCount(1);
    // change the configuration of the cluster_0 to a custom static cluster
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);

      if (cluster_provided_lb_) {
        cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
      }

      envoy::config::cluster::v3::Cluster::CustomClusterType cluster_type;
      cluster_type.set_name(cluster_provided_lb_ ? "envoy.clusters.custom_static_with_lb"
                                                 : "envoy.clusters.custom_static");
      test::integration::clusters::CustomStaticConfig config;
      config.set_priority(10);
      config.set_address(Network::Test::getLoopbackAddressString(ipVersion()));
      config.set_port_value(fake_upstreams_[UpstreamIndex]->localAddress()->ip()->port());
      cluster_type.mutable_typed_config()->PackFrom(config);

      cluster_0->mutable_cluster_type()->CopyFrom(cluster_type);
    });
    HttpIntegrationTest::initialize();
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 1);
  }

  Network::Address::IpVersion ipVersion() const { return version_; }
  bool cluster_provided_lb_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CustomClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(CustomClusterIntegrationTest, TestRouterHeaderOnly) {
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex);
}

TEST_P(CustomClusterIntegrationTest, TestTwoRequests) { testTwoRequests(false); }

TEST_P(CustomClusterIntegrationTest, TestTwoRequestsWithClusterLb) {
  cluster_provided_lb_ = true;
  testTwoRequests(false);
}

TEST_P(CustomClusterIntegrationTest, TestCustomConfig) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  initialize();

  // Verify the cluster is correctly setup with the custom priority
  const auto& cluster_maps = test_server_->server().clusterManager().clusters();
  EXPECT_EQ(1, cluster_maps.active_clusters_.size());
  EXPECT_EQ(1, cluster_maps.active_clusters_.count("cluster_0"));
  const auto& cluster_ref = cluster_maps.active_clusters_.find("cluster_0")->second;
  const auto& hostset_per_priority = cluster_ref.get().prioritySet().hostSetsPerPriority();
  EXPECT_EQ(11, hostset_per_priority.size());
  const Envoy::Upstream::HostSetPtr& host_set = hostset_per_priority[10];
  EXPECT_EQ(1, host_set->hosts().size());
  EXPECT_EQ(1, host_set->healthyHosts().size());
  EXPECT_EQ(10, host_set->priority());
}

} // namespace
} // namespace Envoy
