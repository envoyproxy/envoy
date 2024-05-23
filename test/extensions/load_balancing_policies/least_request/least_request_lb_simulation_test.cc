#include "source/common/common/random_generator.h"
#include "source/extensions/load_balancing_policies/least_request/least_request_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/priority_set.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {
namespace {

// Defines parameters for LeastRequestLoadBalancerWeightTest cases.
struct LRLBTestParams {
  // The total number of hosts.
  uint64_t num_hosts;

  // Number of hosts that are part of the subset.
  uint64_t num_subset_hosts;

  // The weight assigned to each subset host.
  uint64_t weight;

  // The number of active requests each subset host will be loaded with.
  uint64_t active_request_count;

  // An unordered collection of expected selection probabilities for the hosts. The test will simply
  // sort the expected and observed selection probabilities and verify each element is within some
  // expected tolerance. Therefore, the vector does not need to be sorted.
  std::vector<double> expected_selection_probs;
};

void leastRequestLBWeightTest(LRLBTestParams params) {
  constexpr uint64_t num_requests = 100000;

  // Observed selection probabilities must be within tolerance_pct of the expected to pass the test.
  // The expected range is [0,100).
  constexpr double tolerance_pct = 1.0;

  // Validate params.
  ASSERT_GT(params.num_hosts, 0);
  ASSERT_LE(params.num_subset_hosts, params.num_hosts);
  ASSERT_GT(params.weight, 0);
  ASSERT_EQ(params.expected_selection_probs.size(), params.num_hosts);
  ASSERT_LT(tolerance_pct, 100);
  ASSERT_GE(tolerance_pct, 0);

  NiceMock<MockTimeSystem> time_source_;
  HostVector hosts;
  absl::node_hash_map<HostConstSharedPtr, uint64_t> host_hits;
  std::shared_ptr<MockClusterInfo> info{new NiceMock<MockClusterInfo>()};
  for (uint64_t i = 0; i < params.num_hosts; i++) {
    const bool should_weight = i < params.num_subset_hosts;
    auto hostPtr = makeTestHost(info, fmt::format("tcp://10.0.{}.{}:6379", i / 256, i % 256),
                                time_source_, should_weight ? params.weight : 1);
    host_hits[hostPtr] = 0;
    hosts.push_back(hostPtr);
    if (should_weight) {
      hosts.back()->stats().rq_active_.set(params.active_request_count);
    }
  }
  HostVectorConstSharedPtr updated_hosts{new HostVector(hosts)};
  HostsPerLocalitySharedPtr updated_locality_hosts{new HostsPerLocalityImpl(hosts)};
  Random::RandomGeneratorImpl random;
  PrioritySetImpl priority_set;
  priority_set.updateHosts(
      0,
      updateHostsParams(updated_hosts, updated_locality_hosts,
                        std::make_shared<const HealthyHostVector>(*updated_hosts),
                        updated_locality_hosts),
      {}, hosts, {}, random.random(), absl::nullopt);

  Stats::IsolatedStoreImpl stats_store;
  ClusterLbStatNames stat_names(stats_store.symbolTable());
  ClusterLbStats lb_stats{stat_names, *stats_store.rootScope()};
  NiceMock<Runtime::MockLoader> runtime;
  auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig least_request_lb_config;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config;
  LeastRequestLoadBalancer lb_{
      priority_set, nullptr, lb_stats, runtime, random, common_config, least_request_lb_config,
      *time_source};

  for (uint64_t i = 0; i < num_requests; i++) {
    host_hits[lb_.chooseHost(nullptr)]++;
  }

  std::vector<double> observed_pcts;
  for (const auto& host : host_hits) {
    observed_pcts.push_back((static_cast<double>(host.second) / num_requests) * 100);
  }

  std::sort(observed_pcts.begin(), observed_pcts.end());
  std::sort(params.expected_selection_probs.begin(), params.expected_selection_probs.end());
  ASSERT_EQ(observed_pcts.size(), params.expected_selection_probs.size());
  for (uint64_t i = 0; i < observed_pcts.size(); i++) {
    EXPECT_NEAR(params.expected_selection_probs[i], observed_pcts[i], tolerance_pct);
  }
}

// Simulate weighted LR load balancer and verify expected selection probabilities.
TEST(LeastRequestLoadBalancerWeightTest, Weight) {
  LRLBTestParams params;

  // No active requests or weight differences. This should look like uniform random LB.
  params.num_hosts = 3;
  params.num_subset_hosts = 1;
  params.active_request_count = 0;
  params.expected_selection_probs = {33.333, 33.333, 33.333};
  params.weight = 1;
  leastRequestLBWeightTest(params);

  // Single host (out of 3) with lots of in-flight requests. Given that P2C will choose 2 hosts and
  // take the one with higher weight, the only circumstance that the host with many in-flight
  // requests will be picked is if P2C selects it twice.
  params.num_hosts = 3;
  params.num_subset_hosts = 1;
  params.active_request_count = 10;
  params.expected_selection_probs = {44.45, 44.45, 11.1};
  params.weight = 1;
  leastRequestLBWeightTest(params);

  // Same as above, but with 2 hosts. The busy host will only be chosen if P2C picks it for both
  // selections.
  params.num_hosts = 2;
  params.num_subset_hosts = 1;
  params.active_request_count = 10;
  params.expected_selection_probs = {25, 75};
  params.weight = 1;
  leastRequestLBWeightTest(params);

  // Heterogeneous weights with no active requests. This should behave identically to weighted
  // round-robin.
  params.num_hosts = 2;
  params.num_subset_hosts = 1;
  params.active_request_count = 0;
  params.expected_selection_probs = {66.66, 33.33};
  params.weight = 2;
  leastRequestLBWeightTest(params);

  // Same as above, but we'll scale the subset's weight with active requests. With a default
  // active_request_bias of 1.0, the subset host with a single active request will be cut in half,
  // making both hosts have an identical weight.
  params.num_hosts = 2;
  params.num_subset_hosts = 1;
  params.active_request_count = 1;
  params.expected_selection_probs = {50, 50};
  params.weight = 2;
  leastRequestLBWeightTest(params);

  // Same as above, but with 3 hosts.
  params.num_hosts = 3;
  params.num_subset_hosts = 1;
  params.active_request_count = 1;
  params.expected_selection_probs = {33.3, 33.3, 33.3};
  params.weight = 2;
  leastRequestLBWeightTest(params);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
