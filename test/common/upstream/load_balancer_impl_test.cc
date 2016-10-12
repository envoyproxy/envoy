#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::NiceMock;
using testing::Return;

namespace Upstream {

static HostPtr newTestHost(const Upstream::Cluster& cluster, const std::string& url,
                           uint32_t weight = 1) {
  return HostPtr{new HostImpl(cluster, url, false, weight, "")};
}

class RoundRobinLoadBalancerTest : public testing::Test {
public:
  RoundRobinLoadBalancerTest() : stats_(ClusterImplBase::generateStats("", stats_store_)) {}

  NiceMock<MockCluster> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  RoundRobinLoadBalancer lb_{cluster_, nullptr, stats_, runtime_, random_};
};

TEST_F(RoundRobinLoadBalancerTest, NoHosts) { EXPECT_EQ(nullptr, lb_.chooseHost()); }

TEST_F(RoundRobinLoadBalancerTest, SingleHost) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80")};
  cluster_.hosts_ = cluster_.healthy_hosts_;
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
}

TEST_F(RoundRobinLoadBalancerTest, Normal) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  cluster_.hosts_ = cluster_.healthy_hosts_;
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
}

TEST_F(RoundRobinLoadBalancerTest, MaxUnhealthyPanic) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  cluster_.hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:83"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:84"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:85")};

  EXPECT_EQ(cluster_.hosts_[0], lb_.chooseHost());
  EXPECT_EQ(cluster_.hosts_[1], lb_.chooseHost());
  EXPECT_EQ(cluster_.hosts_[2], lb_.chooseHost());

  // Take the threshold back above the panic threshold.
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:83")};

  EXPECT_EQ(cluster_.healthy_hosts_[3], lb_.chooseHost());
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());

  EXPECT_EQ(3UL, stats_.upstream_rq_lb_healthy_panic_.value());
}

TEST_F(RoundRobinLoadBalancerTest, ZoneAwareSmallCluster) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82")};
  cluster_.hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82")};
  cluster_.local_zone_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  cluster_.local_zone_healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  stats_.upstream_zone_count_.set(3UL);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(6));

  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
  EXPECT_EQ(1U, stats_.zone_cluster_too_small_.value());
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());
  EXPECT_EQ(2U, stats_.zone_cluster_too_small_.value());
}

TEST_F(RoundRobinLoadBalancerTest, ZoneAwareRoutingLargeZone) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82")};
  cluster_.hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82")};
  cluster_.local_zone_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  cluster_.local_zone_healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  stats_.upstream_zone_count_.set(3UL);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .Times(2)
      .WillRepeatedly(Return(3));

  // There is only one host in the given zone for zone aware routing.
  EXPECT_EQ(cluster_.local_zone_healthy_hosts_[0], lb_.chooseHost());
  EXPECT_EQ(1U, stats_.zone_over_percentage_.value());
  EXPECT_EQ(cluster_.local_zone_healthy_hosts_[0], lb_.chooseHost());
  EXPECT_EQ(2U, stats_.zone_over_percentage_.value());

  // Disable runtime global zone routing.
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(false));
  EXPECT_EQ(cluster_.healthy_hosts_[2], lb_.chooseHost());
}

TEST_F(RoundRobinLoadBalancerTest, ZoneAwareRoutingSmallZone) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:83"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:84")};
  cluster_.hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:83"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:84")};
  cluster_.local_zone_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  cluster_.local_zone_healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  stats_.upstream_zone_count_.set(3UL);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .Times(2)
      .WillRepeatedly(Return(5));

  // There is only one host in the given zone for zone aware routing.
  EXPECT_CALL(random_, random()).WillOnce(Return(1000));
  EXPECT_EQ(cluster_.local_zone_healthy_hosts_[0], lb_.chooseHost());
  EXPECT_EQ(1U, stats_.zone_routing_sampled_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(6500));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());
  EXPECT_EQ(1U, stats_.zone_routing_no_sampled_.value());
}

TEST_F(RoundRobinLoadBalancerTest, NoZoneAwareRoutingOneZone) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80")};
  cluster_.hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80")};
  cluster_.local_zone_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80")};
  cluster_.local_zone_healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80")};
  stats_.upstream_zone_count_.set(1UL);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100)).Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));

  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
}

TEST_F(RoundRobinLoadBalancerTest, ZoneAwareRoutingNotHealthy) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82")};
  cluster_.hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81"),
                     newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:82")};
  cluster_.local_zone_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  cluster_.local_zone_healthy_hosts_ = {};
  stats_.upstream_zone_count_.set(3UL);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));

  // local zone has no healthy hosts, take from the all healthy hosts.
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());
}

class LeastRequestLoadBalancerTest : public testing::Test {
public:
  LeastRequestLoadBalancerTest() : stats_(ClusterImplBase::generateStats("", stats_store_)) {}

  NiceMock<MockCluster> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  LeastRequestLoadBalancer lb_{cluster_, nullptr, stats_, runtime_, random_};
};

TEST_F(LeastRequestLoadBalancerTest, NoHosts) { EXPECT_EQ(nullptr, lb_.chooseHost()); }

TEST_F(LeastRequestLoadBalancerTest, SingleHost) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80")};
  cluster_.hosts_ = cluster_.healthy_hosts_;

  // Host weight is 1.
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
    stats_.max_host_weight_.set(1UL);
    EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
  }

  // Host weight is 100.
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(2));
    stats_.max_host_weight_.set(100UL);
    EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
  }

  std::vector<HostPtr> empty;
  {
    cluster_.runCallbacks(empty, empty);
    EXPECT_CALL(random_, random()).WillOnce(Return(2));
    EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
  }

  {
    std::vector<HostPtr> remove_hosts;
    remove_hosts.push_back(cluster_.hosts_[0]);
    cluster_.runCallbacks(empty, remove_hosts);
    EXPECT_CALL(random_, random()).Times(0);
    cluster_.healthy_hosts_.clear();
    cluster_.hosts_.clear();
    EXPECT_EQ(nullptr, lb_.chooseHost());
  }
}

TEST_F(LeastRequestLoadBalancerTest, Normal) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  stats_.max_host_weight_.set(1UL);
  cluster_.hosts_ = cluster_.healthy_hosts_;
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());

  cluster_.healthy_hosts_[0]->stats().rq_active_.set(1);
  cluster_.healthy_hosts_[1]->stats().rq_active_.set(2);
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());

  cluster_.healthy_hosts_[0]->stats().rq_active_.set(2);
  cluster_.healthy_hosts_[1]->stats().rq_active_.set(1);
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());
}

TEST_F(LeastRequestLoadBalancerTest, WeightImbalanceRuntimeOff) {
  // Disable weight balancing.
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.weight_enabled", 1))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));

  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80", 1),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81", 3)};
  stats_.max_host_weight_.set(3UL);

  cluster_.hosts_ = cluster_.healthy_hosts_;
  cluster_.healthy_hosts_[0]->stats().rq_active_.set(1);
  cluster_.healthy_hosts_[1]->stats().rq_active_.set(2);

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(1));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());

  EXPECT_CALL(random_, random()).WillOnce(Return(1)).WillOnce(Return(0));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
}

TEST_F(LeastRequestLoadBalancerTest, WeightImbalance) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80", 1),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81", 3)};
  stats_.max_host_weight_.set(3UL);

  cluster_.hosts_ = cluster_.healthy_hosts_;
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.weight_enabled", 1))
      .WillRepeatedly(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));

  // As max weight higher then 1 we do random host pick and keep it for weight requests.
  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());

  // Same host stays as we have to hit it 3 times.
  cluster_.healthy_hosts_[0]->stats().rq_active_.set(2);
  cluster_.healthy_hosts_[1]->stats().rq_active_.set(1);
  EXPECT_CALL(random_, random()).Times(0);
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());

  // Same host stays as we have to hit it 3 times.
  EXPECT_CALL(random_, random()).Times(0);
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());

  // Get random host after previous one was selected 3 times in a row.
  EXPECT_CALL(random_, random()).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());

  // Select second host again.
  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());

  // Set weight to 1, we will switch to the two random hosts mode.
  stats_.max_host_weight_.set(1UL);
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());

  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
}

TEST_F(LeastRequestLoadBalancerTest, WeightImbalanceCallbacks) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80", 1),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81", 3)};
  stats_.max_host_weight_.set(3UL);

  cluster_.hosts_ = cluster_.healthy_hosts_;

  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());

  // Same host stays as we have to hit it 3 times, but we remove it and fire callback.
  std::vector<HostPtr> empty;
  std::vector<HostPtr> hosts_removed;
  hosts_removed.push_back(cluster_.hosts_[1]);
  cluster_.hosts_.erase(cluster_.hosts_.begin() + 1);
  cluster_.healthy_hosts_.erase(cluster_.healthy_hosts_.begin() + 1);
  cluster_.runCallbacks(empty, hosts_removed);

  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
}

class RandomLoadBalancerTest : public testing::Test {
public:
  RandomLoadBalancerTest() : stats_(ClusterImplBase::generateStats("", stats_store_)) {}

  NiceMock<MockCluster> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  RandomLoadBalancer lb_{cluster_, nullptr, stats_, runtime_, random_};
};

TEST_F(RandomLoadBalancerTest, NoHosts) { EXPECT_EQ(nullptr, lb_.chooseHost()); }

TEST_F(RandomLoadBalancerTest, Normal) {
  cluster_.healthy_hosts_ = {newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:80"),
                             newTestHost(Upstream::MockCluster{}, "tcp://127.0.0.1:81")};
  cluster_.hosts_ = cluster_.healthy_hosts_;
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost());
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost());
}

} // Upstream
