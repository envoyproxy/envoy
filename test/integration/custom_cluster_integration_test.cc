#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/snapshot_host_set.h"
#include "test/config/utility.h"
#include "test/integration/clusters/cluster_factory_config.pb.h"
#include "test/integration/clusters/custom_static_cluster.h"
#include "test/integration/http_integration.h"

#include "gmock/gmock.h"
using testing::Contains;
using testing::Key;
using testing::UnorderedElementsAre;

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
      if (!cluster_provided_lb_) {
        test::integration::clusters::CustomStaticConfig1 config;
        config.set_priority(10);
        config.set_address(Network::Test::getLoopbackAddressString(ipVersion()));
        config.set_port_value(fake_upstreams_[UpstreamIndex]->localAddress()->ip()->port());
        config.set_use_snapshot_host_set(use_snapshot_host_set_);
        std::ignore = cluster_type.mutable_typed_config()->PackFrom(config);
      } else {
        test::integration::clusters::CustomStaticConfig2 config;
        config.set_priority(10);
        config.set_address(Network::Test::getLoopbackAddressString(ipVersion()));
        config.set_port_value(fake_upstreams_[UpstreamIndex]->localAddress()->ip()->port());
        std::ignore = cluster_type.mutable_typed_config()->PackFrom(config);
      }

      cluster_0->mutable_cluster_type()->CopyFrom(cluster_type);
      bootstrap.mutable_cluster_manager()->set_enable_deferred_cluster_creation(
          deferred_cluster_creation_);
    });
    HttpIntegrationTest::initialize();
    test_server_->waitForGauge("cluster_manager.active_clusters", testing::Ge(1));
  }

  Network::Address::IpVersion ipVersion() const { return version_; }

  // The main-thread host set at the cluster's configured priority.
  const Upstream::HostSet& clusterHostSet() {
    const auto& cluster_maps = test_server_->server().clusterManager().clusters();
    const auto& cluster_ref = cluster_maps.active_clusters_.find("cluster_0")->second;
    return *cluster_ref.get().prioritySet().hostSetsPerPriority()[10];
  }

  bool cluster_provided_lb_{};
  bool use_snapshot_host_set_{};
  bool deferred_cluster_creation_{};
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
  EXPECT_THAT(cluster_maps.active_clusters_, UnorderedElementsAre(Key("cluster_0")));
  const auto& cluster_ref = cluster_maps.active_clusters_.find("cluster_0")->second;
  const auto& hostset_per_priority = cluster_ref.get().prioritySet().hostSetsPerPriority();
  EXPECT_EQ(11, hostset_per_priority.size());
  const Envoy::Upstream::HostSetPtr& host_set = hostset_per_priority[10];
  EXPECT_EQ(1, host_set->hosts().size());
  EXPECT_EQ(1, host_set->healthyHosts().size());
  EXPECT_EQ(10, host_set->priority());
}

// A cluster whose host sets hand workers snapshots adds its host as a delta, and the host serves
// traffic with the same membership accounting as the flat path.
TEST_P(CustomClusterIntegrationTest, SnapshotHostSetServesTraffic) {
  use_snapshot_host_set_ = true;
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex);

  const Upstream::HostSet& host_set = clusterHostSet();
  EXPECT_NE(nullptr, dynamic_cast<const Upstream::SnapshotHostSet*>(&host_set));
  EXPECT_NE(nullptr, host_set.persistentPartitions());
  EXPECT_EQ(1, host_set.hostCount());
  EXPECT_EQ(1, host_set.healthyHostCount());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_healthy")->value());
}

// With deferred cluster creation, workers build the cluster from the initialization object on first
// use. That object carries the snapshot, so the host must still serve traffic.
TEST_P(CustomClusterIntegrationTest, SnapshotHostSetWithDeferredClusterCreation) {
  use_snapshot_host_set_ = true;
  deferred_cluster_creation_ = true;
  testTwoRequests(false);

  const Upstream::HostSet& host_set = clusterHostSet();
  EXPECT_NE(nullptr, dynamic_cast<const Upstream::SnapshotHostSet*>(&host_set));
  EXPECT_NE(nullptr, host_set.persistentPartitions());
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.membership_total")->value());
}

} // namespace
} // namespace Envoy
