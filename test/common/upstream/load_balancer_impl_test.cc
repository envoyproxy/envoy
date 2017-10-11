#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "common/network/utility.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {

class RoundRobinLoadBalancerTest : public testing::Test {
public:
  RoundRobinLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  void init(bool need_local_cluster) {
    if (need_local_cluster) {
      local_cluster_hosts_.reset(new HostSetImpl());
      lb_.reset(new RoundRobinLoadBalancer(cluster_, local_cluster_hosts_.get(), stats_, runtime_,
                                           random_));
    } else {
      lb_.reset(new RoundRobinLoadBalancer(cluster_, nullptr, stats_, runtime_, random_));
    }
  }

  NiceMock<MockCluster> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  std::shared_ptr<HostSetImpl> local_cluster_hosts_;
  std::shared_ptr<LoadBalancer> lb_;
  std::vector<HostSharedPtr> empty_host_vector_;
};

TEST_F(RoundRobinLoadBalancerTest, NoHosts) {
  init(false);
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

TEST_F(RoundRobinLoadBalancerTest, SingleHost) {
  init(false);
  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80")};
  cluster_.hosts_ = cluster_.healthy_hosts_;
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_F(RoundRobinLoadBalancerTest, Normal) {
  init(false);
  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")};
  cluster_.hosts_ = cluster_.healthy_hosts_;
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_F(RoundRobinLoadBalancerTest, MaxUnhealthyPanic) {
  init(false);
  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")};
  cluster_.hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:82"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:83"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:84"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:85")};

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(cluster_.hosts_[2], lb_->chooseHost(nullptr));

  // Take the threshold back above the panic threshold.
  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:82"),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:83")};

  EXPECT_EQ(cluster_.healthy_hosts_[3], lb_->chooseHost(nullptr));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_->chooseHost(nullptr));

  EXPECT_EQ(3UL, stats_.lb_healthy_panic_.value());
}

TEST_F(RoundRobinLoadBalancerTest, ZoneAwareSmallCluster) {
  init(true);
  HostVectorSharedPtr hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:82")}));
  HostListsSharedPtr hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>(
      {{makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:82")}}));

  cluster_.hosts_ = *hosts;
  cluster_.healthy_hosts_ = *hosts;
  cluster_.healthy_hosts_per_locality_ = *hosts_per_locality;
  local_cluster_hosts_->updateHosts(hosts, hosts, hosts_per_locality, hosts_per_locality,
                                    empty_host_vector_, empty_host_vector_);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(6));

  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(cluster_.healthy_hosts_[2], lb_->chooseHost(nullptr));

  // Cluster size is computed once at zone aware struct regeneration point.
  EXPECT_EQ(1U, stats_.lb_zone_cluster_too_small_.value());

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(1));
  // Trigger reload.
  local_cluster_hosts_->updateHosts(hosts, hosts, hosts_per_locality, hosts_per_locality,
                                    empty_host_vector_, empty_host_vector_);
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][0], lb_->chooseHost(nullptr));
}

TEST_F(RoundRobinLoadBalancerTest, NoZoneAwareDifferentZoneSize) {
  init(true);
  HostVectorSharedPtr hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:82")}));
  HostListsSharedPtr upstream_hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>(
      {{makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:82")}}));
  HostListsSharedPtr local_hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>(
      {{makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80")}}));

  cluster_.healthy_hosts_ = *hosts;
  cluster_.hosts_ = *hosts;
  cluster_.healthy_hosts_per_locality_ = *upstream_hosts_per_locality;
  local_cluster_hosts_->updateHosts(hosts, hosts, local_hosts_per_locality,
                                    local_hosts_per_locality, empty_host_vector_,
                                    empty_host_vector_);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));

  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_number_differs_.value());
}

TEST_F(RoundRobinLoadBalancerTest, ZoneAwareRoutingLargeZoneSwitchOnOff) {
  init(true);
  HostVectorSharedPtr hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:82")}));
  HostListsSharedPtr hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>(
      {{makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:82")}}));

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(3));

  cluster_.healthy_hosts_ = *hosts;
  cluster_.hosts_ = *hosts;
  cluster_.healthy_hosts_per_locality_ = *hosts_per_locality;
  local_cluster_hosts_->updateHosts(hosts, hosts, hosts_per_locality, hosts_per_locality,
                                    empty_host_vector_, empty_host_vector_);

  // There is only one host in the given zone for zone aware routing.
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_all_directly_.value());
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_all_directly_.value());

  // Disable runtime global zone routing.
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(false));
  EXPECT_EQ(cluster_.healthy_hosts_[2], lb_->chooseHost(nullptr));
}

TEST_F(RoundRobinLoadBalancerTest, ZoneAwareRoutingSmallZone) {
  init(true);
  HostVectorSharedPtr upstream_hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:82"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:83"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:84")}));
  HostVectorSharedPtr local_hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:0"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:1"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:2")}));

  HostListsSharedPtr upstream_hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>(
      {{makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
        makeTestHost(cluster_.info_, "tcp://127.0.0.1:82")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:83"),
        makeTestHost(cluster_.info_, "tcp://127.0.0.1:84")}}));

  HostListsSharedPtr local_hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>(
      {{makeTestHost(cluster_.info_, "tcp://127.0.0.1:0")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:1")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:2")}}));

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(5));

  cluster_.healthy_hosts_ = *upstream_hosts;
  cluster_.hosts_ = *upstream_hosts;
  cluster_.healthy_hosts_per_locality_ = *upstream_hosts_per_locality;
  local_cluster_hosts_->updateHosts(local_hosts, local_hosts, local_hosts_per_locality,
                                    local_hosts_per_locality, empty_host_vector_,
                                    empty_host_vector_);

  // There is only one host in the given zone for zone aware routing.
  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_sampled_.value());

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][1], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_cross_zone_.value());
}

TEST_F(RoundRobinLoadBalancerTest, LowPrecisionForDistribution) {
  init(true);

  // upstream_hosts and local_hosts do not matter, zone aware routing is based on per zone hosts.
  HostVectorSharedPtr upstream_hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:80")}));
  cluster_.healthy_hosts_ = *upstream_hosts;
  cluster_.hosts_ = *upstream_hosts;
  HostVectorSharedPtr local_hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:0")}));

  HostListsSharedPtr upstream_hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>());
  HostListsSharedPtr local_hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>());

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(1));

  // The following host distribution with current precision should lead to the no_capacity_left
  // situation.
  // Reuse the same host in all of the structures below to reduce time test takes and this does not
  // impact load balancing logic.
  HostSharedPtr host = makeTestHost(cluster_.info_, "tcp://127.0.0.1:80");
  std::vector<HostSharedPtr> current(45000);

  for (int i = 0; i < 45000; ++i) {
    current[i] = host;
  }
  local_hosts_per_locality->push_back(current);

  current.resize(55000);
  for (int i = 0; i < 55000; ++i) {
    current[i] = host;
  }
  local_hosts_per_locality->push_back(current);

  current.resize(44999);
  for (int i = 0; i < 44999; ++i) {
    current[i] = host;
  }
  upstream_hosts_per_locality->push_back(current);

  current.resize(55001);
  for (int i = 0; i < 55001; ++i) {
    current[i] = host;
  }
  upstream_hosts_per_locality->push_back(current);

  cluster_.healthy_hosts_per_locality_ = *upstream_hosts_per_locality;

  // To trigger update callback.
  local_cluster_hosts_->updateHosts(local_hosts, local_hosts, local_hosts_per_locality,
                                    local_hosts_per_locality, empty_host_vector_,
                                    empty_host_vector_);

  // Force request out of small zone and to randomly select zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  lb_->chooseHost(nullptr);
  EXPECT_EQ(1U, stats_.lb_zone_no_capacity_left_.value());
}

TEST_F(RoundRobinLoadBalancerTest, NoZoneAwareRoutingOneZone) {
  init(true);
  HostVectorSharedPtr hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:80")}));
  HostListsSharedPtr hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>(
      {{makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")}}));

  cluster_.healthy_hosts_ = *hosts;
  cluster_.hosts_ = *hosts;
  cluster_.healthy_hosts_per_locality_ = *hosts_per_locality;
  local_cluster_hosts_->updateHosts(hosts, hosts, hosts_per_locality, hosts_per_locality,
                                    empty_host_vector_, empty_host_vector_);
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_F(RoundRobinLoadBalancerTest, NoZoneAwareRoutingNotHealthy) {
  init(true);
  HostVectorSharedPtr hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.2:80")}));
  HostListsSharedPtr hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>(
      {{},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
        makeTestHost(cluster_.info_, "tcp://127.0.0.2:80")}}));

  cluster_.healthy_hosts_ = *hosts;
  cluster_.hosts_ = *hosts;
  cluster_.healthy_hosts_per_locality_ = *hosts_per_locality;
  local_cluster_hosts_->updateHosts(hosts, hosts, hosts_per_locality, hosts_per_locality,
                                    empty_host_vector_, empty_host_vector_);

  // local zone has no healthy hosts, take from the all healthy hosts.
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_F(RoundRobinLoadBalancerTest, NoZoneAwareRoutingLocalEmpty) {
  init(true);
  HostVectorSharedPtr upstream_hosts(
      new std::vector<HostSharedPtr>({makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                                      makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")}));
  HostVectorSharedPtr local_hosts(new std::vector<HostSharedPtr>({}, {}));

  HostListsSharedPtr upstream_hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>(
      {{makeTestHost(cluster_.info_, "tcp://127.0.0.1:80")},
       {makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")}}));
  HostListsSharedPtr local_hosts_per_locality(
      new std::vector<std::vector<HostSharedPtr>>({{}, {}}));

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillOnce(Return(50))
      .WillOnce(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillOnce(Return(1));

  cluster_.healthy_hosts_ = *upstream_hosts;
  cluster_.hosts_ = *upstream_hosts;
  cluster_.healthy_hosts_per_locality_ = *upstream_hosts_per_locality;
  local_cluster_hosts_->updateHosts(local_hosts, local_hosts, local_hosts_per_locality,
                                    local_hosts_per_locality, empty_host_vector_,
                                    empty_host_vector_);

  // Local cluster is not OK, we'll do regular routing.
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_healthy_panic_.value());
  EXPECT_EQ(1U, stats_.lb_local_cluster_not_ok_.value());
}

class LeastRequestLoadBalancerTest : public testing::Test {
public:
  LeastRequestLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  NiceMock<MockCluster> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  LeastRequestLoadBalancer lb_{cluster_, nullptr, stats_, runtime_, random_};
};

TEST_F(LeastRequestLoadBalancerTest, NoHosts) { EXPECT_EQ(nullptr, lb_.chooseHost(nullptr)); }

TEST_F(LeastRequestLoadBalancerTest, SingleHost) {
  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80")};
  cluster_.hosts_ = cluster_.healthy_hosts_;

  // Host weight is 1.
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
    stats_.max_host_weight_.set(1UL);
    EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));
  }

  // Host weight is 100.
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(2));
    stats_.max_host_weight_.set(100UL);
    EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));
  }

  std::vector<HostSharedPtr> empty;
  {
    cluster_.runCallbacks(empty, empty);
    EXPECT_CALL(random_, random()).WillOnce(Return(2));
    EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));
  }

  {
    std::vector<HostSharedPtr> remove_hosts;
    remove_hosts.push_back(cluster_.hosts_[0]);
    cluster_.runCallbacks(empty, remove_hosts);
    EXPECT_CALL(random_, random()).Times(0);
    cluster_.healthy_hosts_.clear();
    cluster_.hosts_.clear();
    EXPECT_EQ(nullptr, lb_.chooseHost(nullptr));
  }
}

TEST_F(LeastRequestLoadBalancerTest, Normal) {
  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")};
  stats_.max_host_weight_.set(1UL);
  cluster_.hosts_ = cluster_.healthy_hosts_;
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost(nullptr));

  cluster_.healthy_hosts_[0]->stats().rq_active_.set(1);
  cluster_.healthy_hosts_[1]->stats().rq_active_.set(2);
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));

  cluster_.healthy_hosts_[0]->stats().rq_active_.set(2);
  cluster_.healthy_hosts_[1]->stats().rq_active_.set(1);
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost(nullptr));
}

TEST_F(LeastRequestLoadBalancerTest, WeightImbalanceRuntimeOff) {
  // Disable weight balancing.
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.weight_enabled", 1))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));

  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80", 1),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:81", 3)};
  stats_.max_host_weight_.set(3UL);

  cluster_.hosts_ = cluster_.healthy_hosts_;
  cluster_.healthy_hosts_[0]->stats().rq_active_.set(1);
  cluster_.healthy_hosts_[1]->stats().rq_active_.set(2);

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(1));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));

  EXPECT_CALL(random_, random()).WillOnce(Return(1)).WillOnce(Return(0));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));
}

TEST_F(LeastRequestLoadBalancerTest, WeightImbalance) {
  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80", 1),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:81", 3)};
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
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost(nullptr));

  // Same host stays as we have to hit it 3 times.
  cluster_.healthy_hosts_[0]->stats().rq_active_.set(2);
  cluster_.healthy_hosts_[1]->stats().rq_active_.set(1);
  EXPECT_CALL(random_, random()).Times(0);
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost(nullptr));

  // Same host stays as we have to hit it 3 times.
  EXPECT_CALL(random_, random()).Times(0);
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost(nullptr));

  // Get random host after previous one was selected 3 times in a row.
  EXPECT_CALL(random_, random()).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));

  // Select second host again.
  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost(nullptr));

  // Set weight to 1, we will switch to the two random hosts mode.
  stats_.max_host_weight_.set(1UL);
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost(nullptr));

  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));
}

TEST_F(LeastRequestLoadBalancerTest, WeightImbalanceCallbacks) {
  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80", 1),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:81", 3)};
  stats_.max_host_weight_.set(3UL);

  cluster_.hosts_ = cluster_.healthy_hosts_;

  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost(nullptr));

  // Same host stays as we have to hit it 3 times, but we remove it and fire callback.
  std::vector<HostSharedPtr> empty;
  std::vector<HostSharedPtr> hosts_removed;
  hosts_removed.push_back(cluster_.hosts_[1]);
  cluster_.hosts_.erase(cluster_.hosts_.begin() + 1);
  cluster_.healthy_hosts_.erase(cluster_.healthy_hosts_.begin() + 1);
  cluster_.runCallbacks(empty, hosts_removed);

  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));
}

class RandomLoadBalancerTest : public testing::Test {
public:
  RandomLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  NiceMock<MockCluster> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  RandomLoadBalancer lb_{cluster_, nullptr, stats_, runtime_, random_};
};

TEST_F(RandomLoadBalancerTest, NoHosts) { EXPECT_EQ(nullptr, lb_.chooseHost(nullptr)); }

TEST_F(RandomLoadBalancerTest, Normal) {
  cluster_.healthy_hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                             makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")};
  cluster_.hosts_ = cluster_.healthy_hosts_;
  EXPECT_CALL(random_, random()).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(cluster_.healthy_hosts_[0], lb_.chooseHost(nullptr));
  EXPECT_EQ(cluster_.healthy_hosts_[1], lb_.chooseHost(nullptr));
}

TEST(LoadBalancerSubsetInfoImplTest, DefaultConfigIsDiabled) {
  auto subset_info =
      LoadBalancerSubsetInfoImpl(envoy::api::v2::Cluster::LbSubsetConfig::default_instance());

  EXPECT_FALSE(subset_info.isEnabled());
  EXPECT_TRUE(subset_info.fallbackPolicy() == envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK);
  EXPECT_EQ(subset_info.defaultSubset().fields_size(), 0);
  EXPECT_EQ(subset_info.subsetKeys().size(), 0);
}

TEST(LoadBalancerSubsetInfoImplTest, SubsetConfig) {
  auto subset_value = ProtobufWkt::Value();
  subset_value.set_string_value("the value");

  auto subset_config = envoy::api::v2::Cluster::LbSubsetConfig::default_instance();
  subset_config.set_fallback_policy(envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET);
  subset_config.mutable_default_subset()->mutable_fields()->insert({"key", subset_value});
  auto subset_selector = subset_config.mutable_subset_selectors()->Add();
  subset_selector->add_keys("selector_key");

  auto subset_info = LoadBalancerSubsetInfoImpl(subset_config);

  EXPECT_TRUE(subset_info.isEnabled());
  EXPECT_TRUE(subset_info.fallbackPolicy() ==
              envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET);
  EXPECT_EQ(subset_info.defaultSubset().fields_size(), 1);
  EXPECT_EQ(subset_info.defaultSubset().fields().at("key").string_value(),
            std::string("the value"));
  EXPECT_EQ(subset_info.subsetKeys().size(), 1);
  EXPECT_EQ(subset_info.subsetKeys()[0], std::set<std::string>({"selector_key"}));
}

} // namespace Upstream
} // namespace Envoy
