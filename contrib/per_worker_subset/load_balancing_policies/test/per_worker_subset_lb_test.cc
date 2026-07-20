#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "envoy/upstream/upstream.h"

#include "source/common/common/random_generator.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/simulated_time_system.h"

#include "contrib/per_worker_subset/load_balancing_policies/source/per_worker_subset_lb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PerWorkerSubset {
namespace {

class PerWorkerSubsetLoadBalancerTest : public testing::Test {
public:
  void makeHosts(size_t n) {
    hosts_.clear();
    for (size_t i = 0; i < n; ++i) {
      auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
      // MockHost's ``coarseHealth()`` defaults to ``Health::Unhealthy``
      // (enum value 0). Force it to Healthy so the synthetic priority set
      // (used by the inner stock LBs in EnvoyRoundRobin and EnvoyP2C modes)
      // classifies these as healthy in its ``partitionHosts()`` split.
      // Outer MockHostSet bookkeeping (``host_set_.healthy_hosts_``
      // assignment below) is what the SimpleRoundRobin path reads, so this
      // is purely for the inner LB plumbing.
      ON_CALL(*host, coarseHealth()).WillByDefault(Return(Upstream::Host::Health::Healthy));
      hosts_.push_back(host);
    }
    host_set_.hosts_ = hosts_;
    host_set_.healthy_hosts_ = hosts_;
  }

  NiceMock<Upstream::MockPrioritySet> priority_set_;
  Upstream::MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  Stats::IsolatedStoreImpl stats_;
  Upstream::ClusterLbStatNames stat_names_{stats_.symbolTable()};
  Upstream::ClusterLbStats lb_stats_{stat_names_, *stats_.rootScope()};
  NiceMock<Runtime::MockLoader> runtime_;
  ::Envoy::Random::RandomGeneratorImpl random_;
  Event::SimulatedTimeSystem time_system_;
  std::vector<Upstream::HostSharedPtr> hosts_;
  // Resolved total worker count W (fixed at config-load time in production).
  // Tests set it per scenario.
  uint32_t total_workers_ = 1;
};

// RANDOM_PARTITIONS: ``subset_size=4`` against a 10-host cluster -> exactly 4
// distinct hosts visible to ``chooseHost()``, regardless of how many
// requests we send.
TEST_F(PerWorkerSubsetLoadBalancerTest, RandomPartitionsIsBoundedBySubsetSize) {
  makeHosts(10);
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/4, PartitioningStrategy::RandomPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 100; ++i) {
    auto host = lb.chooseHost(nullptr).host;
    ASSERT_NE(host, nullptr);
    seen.insert(host);
  }
  EXPECT_EQ(seen.size(), 4);
}

// RANDOM_PARTITIONS: ``subset_size`` larger than the cluster -> subset == full
// set.
TEST_F(PerWorkerSubsetLoadBalancerTest, RandomOversizedSubsetUsesAllHosts) {
  makeHosts(5);
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/100, PartitioningStrategy::RandomPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 50; ++i) {
    auto host = lb.chooseHost(nullptr).host;
    ASSERT_NE(host, nullptr);
    seen.insert(host);
  }
  EXPECT_EQ(seen.size(), 5);
}

// Empty cluster -> ``nullptr``.
TEST_F(PerWorkerSubsetLoadBalancerTest, EmptyClusterReturnsNullptr) {
  makeHosts(0);
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/4, PartitioningStrategy::RandomPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});
  EXPECT_EQ(lb.chooseHost(nullptr).host, nullptr);
}

// All hosts unhealthy -> per-worker fallback uses the full host list.
TEST_F(PerWorkerSubsetLoadBalancerTest, AllUnhealthyFallsBackToFullHostList) {
  makeHosts(8);
  host_set_.healthy_hosts_.clear();
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/4, PartitioningStrategy::RandomPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 50; ++i) {
    auto host = lb.chooseHost(nullptr).host;
    ASSERT_NE(host, nullptr) << "fallback should still return hosts";
    seen.insert(host);
  }
  EXPECT_EQ(seen.size(), 4);
}

// RANDOM_PARTITIONS membership update retains surviving sampled hosts and
// fills vacancies from the new membership.
TEST_F(PerWorkerSubsetLoadBalancerTest, RandomPartitionsMembershipUpdateRebuildsSubset) {
  makeHosts(10);
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/4, PartitioningStrategy::RandomPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  for (int i = 0; i < 50; ++i) {
    (void)lb.chooseHost(nullptr).host;
  }

  makeHosts(8);
  priority_set_.runUpdateCallbacks(/*priority=*/0, /*added=*/hosts_,
                                   /*removed=*/{});

  std::set<Upstream::HostConstSharedPtr> seen_after;
  for (int i = 0; i < 50; ++i) {
    seen_after.insert(lb.chooseHost(nullptr).host);
  }
  for (const auto& h : seen_after) {
    EXPECT_TRUE(std::find(hosts_.begin(), hosts_.end(), h) != hosts_.end());
  }
  EXPECT_EQ(seen_after.size(), 4);
}

TEST_F(PerWorkerSubsetLoadBalancerTest, RandomPartitionsRetainsAssignmentAcrossHealthUpdates) {
  makeHosts(10);
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/4, PartitioningStrategy::RandomPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> initial;
  for (int i = 0; i < 50; ++i) {
    initial.insert(lb.chooseHost(nullptr).host);
  }
  ASSERT_EQ(initial.size(), 4);

  const auto unhealthy = *initial.begin();
  const auto unhealthy_it = std::find(hosts_.begin(), hosts_.end(), unhealthy);
  ASSERT_NE(unhealthy_it, hosts_.end());
  auto& unhealthy_mock = static_cast<NiceMock<Upstream::MockHost>&>(**unhealthy_it);
  ON_CALL(unhealthy_mock, coarseHealth()).WillByDefault(Return(Upstream::Host::Health::Unhealthy));
  host_set_.healthy_hosts_.erase(
      std::remove(host_set_.healthy_hosts_.begin(), host_set_.healthy_hosts_.end(), unhealthy),
      host_set_.healthy_hosts_.end());
  priority_set_.runUpdateCallbacks(/*priority=*/0, /*added=*/{}, /*removed=*/{});

  std::set<Upstream::HostConstSharedPtr> while_unhealthy;
  for (int i = 0; i < 50; ++i) {
    while_unhealthy.insert(lb.chooseHost(nullptr).host);
  }
  EXPECT_EQ(while_unhealthy.size(), 3);
  EXPECT_EQ(while_unhealthy.count(unhealthy), 0);
  for (const auto& host : while_unhealthy) {
    EXPECT_EQ(initial.count(host), 1);
  }

  ON_CALL(unhealthy_mock, coarseHealth()).WillByDefault(Return(Upstream::Host::Health::Healthy));
  host_set_.healthy_hosts_.push_back(std::const_pointer_cast<Upstream::Host>(unhealthy));
  priority_set_.runUpdateCallbacks(/*priority=*/0, /*added=*/{}, /*removed=*/{});

  std::set<Upstream::HostConstSharedPtr> after_recovery;
  for (int i = 0; i < 50; ++i) {
    after_recovery.insert(lb.chooseHost(nullptr).host);
  }
  EXPECT_EQ(after_recovery, initial);
}

// EQUAL_PARTITIONS auto-computes ``K = ceil(N/W)``. With ``N=64`` and ``W=8``,
// ``K=8``; worker 0 and worker 1 see distinct 8-host slices.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsAutoComputesDisjointSubsets) {
  makeHosts(64);
  total_workers_ = 8;
  PerWorkerSubsetLoadBalancer lb_a(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});
  PerWorkerSubsetLoadBalancer lb_b(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/1, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen_a, seen_b;
  for (int i = 0; i < 50; ++i) {
    seen_a.insert(lb_a.chooseHost(nullptr).host);
    seen_b.insert(lb_b.chooseHost(nullptr).host);
  }
  EXPECT_EQ(seen_a.size(), 8);
  EXPECT_EQ(seen_b.size(), 8);

  std::set<Upstream::HostConstSharedPtr> intersection;
  std::set_intersection(seen_a.begin(), seen_a.end(), seen_b.begin(), seen_b.end(),
                        std::inserter(intersection, intersection.begin()));
  EXPECT_TRUE(intersection.empty()) << "expected disjoint partitions";
}

// EQUAL_PARTITIONS: same ``(worker_id, membership)`` reproduces same subset.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsIsDeterministic) {
  makeHosts(20);
  total_workers_ = 4;
  PerWorkerSubsetLoadBalancer lb1(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/2, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});
  std::set<Upstream::HostConstSharedPtr> seen1;
  for (int i = 0; i < 30; ++i) {
    seen1.insert(lb1.chooseHost(nullptr).host);
  }

  PerWorkerSubsetLoadBalancer lb2(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/2, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});
  std::set<Upstream::HostConstSharedPtr> seen2;
  for (int i = 0; i < 30; ++i) {
    seen2.insert(lb2.chooseHost(nullptr).host);
  }
  EXPECT_EQ(seen1, seen2);
}

// EQUAL_PARTITIONS: ``subset_size >= N`` -> "no subsetting"; each worker
// takes the entire cluster.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsDisableSubsettingWhenOversized) {
  makeHosts(5);
  total_workers_ = 8;
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/100, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 50; ++i) {
    seen.insert(lb.chooseHost(nullptr).host);
  }
  EXPECT_EQ(seen.size(), 5) << "expected all hosts visible when subset_size >= N";
}

// EQUAL_PARTITIONS: with ``W=1`` and a multi-host cluster, worker 0 holds
// every host (degenerate case -- equivalent to round-robin). Mirrors what
// happens when bootstrap concurrency resolves to 1.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsSingleWorkerTakesAllHosts) {
  makeHosts(40);
  total_workers_ = 1;
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 100; ++i) {
    seen.insert(lb.chooseHost(nullptr).host);
  }
  EXPECT_EQ(seen.size(), 40) << "single-worker should cover every host";
}

// EQUAL_PARTITIONS: ``N < W`` (small cluster, many workers). ``K = ceil(N/W)``
// clamps to 1, so every worker pins to a single host via the
// ``worker_id * k % N = worker_id % N`` mapping. Asserts that:
//   - every worker still produces a non-empty subset (no idle workers),
//   - workers sharing ``(worker_id % N)`` land on the same host,
//   - workers in different classes land on different hosts,
//   - all N hosts are covered, with ``W/N +/- 1`` workers each.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsSmallClusterPinsOneHostPerWorker) {
  const size_t N = 5;
  const uint32_t W = 128;
  makeHosts(N);
  total_workers_ = W;

  auto subset_for = [&](uint32_t worker_id) {
    PerWorkerSubsetLoadBalancer lb(
        priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
        /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
        HostSelectionStrategy::SimpleRoundRobin, worker_id, total_workers_,
        /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});
    std::set<Upstream::HostConstSharedPtr> seen;
    for (size_t i = 0; i < N; ++i) {
      seen.insert(lb.chooseHost(nullptr).host);
    }
    return seen;
  };

  std::map<Upstream::HostConstSharedPtr, int> workers_per_host;
  std::vector<Upstream::HostConstSharedPtr> pin_by_class(N);
  for (uint32_t w = 0; w < W; ++w) {
    auto seen = subset_for(w);
    EXPECT_EQ(seen.size(), 1u) << "worker " << w << " expected subset size 1";
    const auto& host = *seen.begin();
    if (w < N) {
      pin_by_class[w] = host;
    } else {
      EXPECT_EQ(host, pin_by_class[w % N])
          << "worker " << w << " (class " << (w % N) << ") landed on a different host";
    }
    workers_per_host[host]++;
  }

  // No host is starved; distribution is ``W/N +/- 1`` (128/5 = 25 r 3 ->
  // three hosts get 26, two get 25).
  EXPECT_EQ(workers_per_host.size(), N);
  for (const auto& [host, count] : workers_per_host) {
    EXPECT_GE(count, static_cast<int>(W / N));
    EXPECT_LE(count, static_cast<int>(W / N) + 1);
  }

  // Different ``(worker_id % N)`` classes land on distinct hosts.
  std::set<Upstream::HostConstSharedPtr> distinct(pin_by_class.begin(), pin_by_class.end());
  EXPECT_EQ(distinct.size(), N);
}

// EQUAL_PARTITIONS: a non-zero ``envoy_seed`` rotates the starting position
// so the same ``worker_id`` on two LB instances (think two Envoys in the
// fleet) covers a different host slice. Same partition shape and same K,
// just shifted. Within a single Envoy disjointness is still preserved
// (covered elsewhere by ``EqualPartitionsAutoComputesDisjointSubsets``).
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsEnvoySeedRotatesStart) {
  makeHosts(40);
  total_workers_ = 5; // K = ceil(40/5) = 8
  // envoy_seed=0 -> start = (0 + 0*8) % 40 = 0
  PerWorkerSubsetLoadBalancer lb_a(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50,
      /*envoy_seed=*/0, /*slow_start_config=*/{});
  // envoy_seed=13 -> start = (13 + 0*8) % 40 = 13
  PerWorkerSubsetLoadBalancer lb_b(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50,
      /*envoy_seed=*/13, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen_a, seen_b;
  for (int i = 0; i < 50; ++i) {
    seen_a.insert(lb_a.chooseHost(nullptr).host);
    seen_b.insert(lb_b.chooseHost(nullptr).host);
  }
  EXPECT_EQ(seen_a.size(), 8);
  EXPECT_EQ(seen_b.size(), 8);
  EXPECT_NE(seen_a, seen_b) << "different envoy_seed must rotate the starting host index";

  // K=8 slices starting at 0 vs 13 (offset > K) should be disjoint.
  std::set<Upstream::HostConstSharedPtr> intersection;
  std::set_intersection(seen_a.begin(), seen_a.end(), seen_b.begin(), seen_b.end(),
                        std::inserter(intersection, intersection.begin()));
  EXPECT_TRUE(intersection.empty())
      << "K=8 slices starting at 0 vs 13 (offset > K) should be disjoint";
}

// EQUAL_PARTITIONS + ENVOY_P2C: subset partitioning matches EQUAL_PARTITIONS
// (same address-sorted slice + ``worker_id`` modular assignment); the
// difference is the within-subset pick. With balanced active-request
// counts, P2C still confines picks to the worker's subset, never to
// outside hosts.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsEnvoyP2CStaysInsideSubset) {
  makeHosts(64);
  total_workers_ = 8;
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions, HostSelectionStrategy::EnvoyP2C,
      /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 200; ++i) {
    seen.insert(lb.chooseHost(nullptr).host);
  }
  // Same K as EqualPartitions: ceil(64/8) = 8.
  EXPECT_EQ(seen.size(), 8);
}

// EQUAL_PARTITIONS + ENVOY_P2C: single-host subset (``N < W`` small-cluster
// case) degenerates to "pick the only host" -- the inner LB has just one
// host in its synthetic priority set to choose from.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsEnvoyP2CSingleHostSubset) {
  makeHosts(5);
  total_workers_ = 128;
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions, HostSelectionStrategy::EnvoyP2C,
      /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 50; ++i) {
    seen.insert(lb.chooseHost(nullptr).host);
  }
  EXPECT_EQ(seen.size(), 1u);
}

// EQUAL_PARTITIONS + ENVOY_P2C: when the subset has multiple hosts and one
// of them has a much higher active-request count, the stock LeastRequest LB
// inside the subset prefers the less-busy hosts via its P2C tie-breaking.
// Drives one subset host's ``rq_active_`` up, then verifies it gets picked
// far less than its proportional share.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsEnvoyP2CSkewsAwayFromBusyHost) {
  makeHosts(8);
  total_workers_ = 1; // single worker -> subset is full cluster
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions, HostSelectionStrategy::EnvoyP2C,
      /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  // Pump ``rq_active_`` on ``host[3]`` way above the others so P2C
  // consistently disprefers it. Mock host stats are real PrimitiveCounter
  // -- ``inc()`` bumps the value ``chooseHost()`` reads.
  for (int i = 0; i < 1000; ++i) {
    hosts_[3]->stats().rq_active_.inc();
  }

  int busy_picks = 0;
  const int iterations = 2000;
  for (int i = 0; i < iterations; ++i) {
    if (lb.chooseHost(nullptr).host == hosts_[3]) {
      ++busy_picks;
    }
  }
  // Under uniform RR among 8 hosts each host is picked ~12.5% of the time.
  // Stock LeastRequest P2C samples two random indices (with replacement)
  // and returns the lower-``rq_active`` host, so the busy host can only win
  // when both random draws happen to land on it: ``P = (1/8)^2 ~ 1.6%``,
  // or ~31 picks over 2000. The < 5% bound below leaves comfortable margin.
  EXPECT_LT(busy_picks, iterations / 20)
      << "busy host picked " << busy_picks << "/" << iterations
      << " -- expected near-zero under P2C with one wildly busier host";
}

// EQUAL_PARTITIONS + ENVOY_ROUND_ROBIN: stock RoundRobin delegates pick to
// the worker's subset. Distribution should be roughly uniform across the K
// subset hosts.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsEnvoyRoundRobinCoversSubset) {
  makeHosts(64);
  total_workers_ = 8; // K = ceil(64/8) = 8
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::EnvoyRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 200; ++i) {
    seen.insert(lb.chooseHost(nullptr).host);
  }
  EXPECT_EQ(seen.size(), 8) << "expected all 8 subset hosts to be picked under RR";
}

// EQUAL_PARTITIONS + ENVOY_ROUND_ROBIN: single-host subset (``N < W``)
// routes every request to the single subset host.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsEnvoyRoundRobinSingleHostSubset) {
  makeHosts(5);
  total_workers_ = 128;
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::EnvoyRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 50; ++i) {
    seen.insert(lb.chooseHost(nullptr).host);
  }
  EXPECT_EQ(seen.size(), 1u);
}

// RANDOM_PARTITIONS per-worker fallback: when healthy hosts are too few to fill
// the requested ``subset_size``, fall back to sampling from all hosts.
TEST_F(PerWorkerSubsetLoadBalancerTest, RandomPartitionsFallsBackWhenHealthyTooFew) {
  makeHosts(100);
  // 40 healthy, but subset_size=60 -- can't fill 60 from 40 healthy, so the
  // per-worker fallback samples from the full 100-host pool.
  host_set_.healthy_hosts_.assign(hosts_.begin(), hosts_.begin() + 40);
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/60, PartitioningStrategy::RandomPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 500; ++i) {
    seen.insert(lb.chooseHost(nullptr).host);
  }
  // Some sampled hosts must be from the unhealthy half.
  bool any_unhealthy = false;
  for (size_t i = 40; i < 100; ++i) {
    if (seen.count(hosts_[i])) {
      any_unhealthy = true;
      break;
    }
  }
  EXPECT_TRUE(any_unhealthy) << "per-worker fallback should include unhealthy hosts in the pool";
  // And fallback counter incremented.
  EXPECT_GT(stats_.counterFromString("lb_per_worker_subset_slice_fallback").value(), 0u);
}

// Above-threshold healthy fraction: stay healthy-only (no fallback).
TEST_F(PerWorkerSubsetLoadBalancerTest, NoFallbackWhenHealthyAboveThreshold) {
  makeHosts(100);
  // 80 of 100 healthy -> 80% >= 50% -> no fallback. Subset comes from
  // ``healthy[0..79]`` only; unhealthy hosts must never be picked.
  host_set_.healthy_hosts_.assign(hosts_.begin(), hosts_.begin() + 80);
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/8, PartitioningStrategy::RandomPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 200; ++i) {
    seen.insert(lb.chooseHost(nullptr).host);
  }
  for (size_t i = 80; i < 100; ++i) {
    EXPECT_EQ(seen.count(hosts_[i]), 0u) << "unhealthy host appeared without fallback";
  }
}

// EQUAL_PARTITIONS per-worker fallback: with mid-range healthy fraction
// (40 of 100 = 40% < 50% threshold), a worker whose slice happens to land
// in the unhealthy band should fall back to the full slice (including
// unhealthy hosts). A worker whose slice is all healthy should NOT fall
// back.
TEST_F(PerWorkerSubsetLoadBalancerTest, EqualPartitionsPerWorkerFallbackOnlyAffectsBadSlices) {
  makeHosts(100);
  // First 40 hosts healthy; last 60 unhealthy. ``rebuildEqualPartition``
  // reads health via ``host->coarseHealth()`` (not host_set's
  // ``healthy_hosts`` vector), so override the mock per-host.
  for (size_t i = 40; i < 100; ++i) {
    ON_CALL(static_cast<NiceMock<Upstream::MockHost>&>(*hosts_[i]), coarseHealth())
        .WillByDefault(Return(Upstream::Host::Health::Unhealthy));
  }
  host_set_.healthy_hosts_.assign(hosts_.begin(), hosts_.begin() + 40);
  // With W=10 and N=100, K=10. Worker 0's slice = ``sorted[0..9]`` (healthy).
  // Worker 5's slice = ``sorted[50..59]`` (entirely unhealthy band).
  total_workers_ = 10;

  // Worker 0's slice is fully healthy -> no fallback.
  PerWorkerSubsetLoadBalancer lb_healthy(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/0, total_workers_,
      /*fallback_threshold=*/50,
      /*envoy_seed=*/0, /*slow_start_config=*/{});
  const uint64_t fallbacks_after_healthy =
      stats_.counterFromString("lb_per_worker_subset_slice_fallback").value();

  // Worker 5's slice is fully unhealthy -> fallback, includes unhealthy hosts.
  PerWorkerSubsetLoadBalancer lb_unhealthy(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/5, total_workers_,
      /*fallback_threshold=*/50,
      /*envoy_seed=*/0, /*slow_start_config=*/{});
  EXPECT_GT(stats_.counterFromString("lb_per_worker_subset_slice_fallback").value(),
            fallbacks_after_healthy)
      << "worker with all-unhealthy slice must trigger per-worker fallback";
}

// ``fallback_threshold = 0`` disables the per-worker percent check; each
// worker keeps whatever healthy hosts are in its slice (and accepts an
// empty subset if the slice has zero healthy, unless that triggers the
// unconditional empty-healthy fallback).
TEST_F(PerWorkerSubsetLoadBalancerTest, FallbackThresholdZeroDisablesPercentCheck) {
  makeHosts(20);
  // 12 of 20 healthy. With W=4 K=5, worker 2's slice = ``sorted[10..14]``.
  // Hosts ``[12..14]`` are unhealthy. ``healthy_in_slice = 2``, ``K = 5``
  // -> 40%. With threshold=50: would fall back. With threshold=0: stays on
  // healthy. Override ``coarseHealth()`` per-host since
  // ``rebuildEqualPartition`` reads ``host->coarseHealth()`` directly,
  // not host_set's ``healthy_hosts`` vector.
  for (size_t i = 12; i < 20; ++i) {
    ON_CALL(static_cast<NiceMock<Upstream::MockHost>&>(*hosts_[i]), coarseHealth())
        .WillByDefault(Return(Upstream::Host::Health::Unhealthy));
  }
  host_set_.healthy_hosts_.assign(hosts_.begin(), hosts_.begin() + 12);
  total_workers_ = 4;
  PerWorkerSubsetLoadBalancer lb(
      priority_set_, lb_stats_, *stats_.rootScope(), runtime_, random_, time_system_,
      /*subset_size=*/0, PartitioningStrategy::EqualPartitions,
      HostSelectionStrategy::SimpleRoundRobin, /*worker_id=*/2, total_workers_,
      /*fallback_threshold=*/0, /*envoy_seed=*/0, /*slow_start_config=*/{});

  std::set<Upstream::HostConstSharedPtr> seen;
  for (int i = 0; i < 200; ++i) {
    auto host = lb.chooseHost(nullptr).host;
    if (host)
      seen.insert(host);
  }
  // Only ``host[10]`` and ``host[11]`` (the healthy ones in worker 2's
  // slice) should appear. ``threshold=0`` disables the percent-based
  // fallback.
  for (size_t i = 12; i < 20; ++i) {
    EXPECT_EQ(seen.count(hosts_[i]), 0u)
        << "threshold=0 should keep LB on healthy slice members only";
  }
}

} // namespace
} // namespace PerWorkerSubset
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
