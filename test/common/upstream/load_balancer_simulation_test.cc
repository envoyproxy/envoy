#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/fmt.h"
#include "source/common/common/random_generator.h"
#include "source/common/network/utility.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/load_balancing_policies/least_request/least_request_lb.h"
#include "source/extensions/load_balancing_policies/random/random_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/priority_set.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

static HostSharedPtr newTestHost(Upstream::ClusterInfoConstSharedPtr cluster,
                                 const std::string& url, TimeSource& time_source,
                                 uint32_t weight = 1, const std::string& zone = "") {
  envoy::config::core::v3::Locality locality;
  locality.set_zone(zone);
  return HostSharedPtr{new HostImpl(
      cluster, "", *Network::Utility::resolveUrl(url), nullptr, nullptr, weight, locality,
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0,
      envoy::config::core::v3::UNKNOWN, time_source)};
}

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

/**
 * This test is for simulation only and should not be run as part of unit tests.
 */
class DISABLED_SimulationTest : public testing::Test { // NOLINT(readability-identifier-naming)
public:
  DISABLED_SimulationTest()
      : stat_names_(stats_store_.symbolTable()), stats_(stat_names_, *stats_store_.rootScope()) {
    ON_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50U))
        .WillByDefault(Return(50U));
    ON_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
        .WillByDefault(Return(6));
  }

  /**
   * Run simulation with given parameters. Generate statistics on per host requests.
   *
   * @param originating_cluster total number of hosts in each zone in originating cluster.
   * @param all_destination_cluster total number of hosts in each zone in upstream cluster.
   * @param healthy_destination_cluster total number of healthy hosts in each zone in upstream
   * cluster.
   */
  void run(std::vector<uint32_t> originating_cluster, std::vector<uint32_t> all_destination_cluster,
           std::vector<uint32_t> healthy_destination_cluster) {
    local_priority_set_ = new PrioritySetImpl;
    // TODO(mattklein123): make load balancer per originating cluster host.
    RandomLoadBalancer lb(priority_set_, local_priority_set_, stats_, runtime_, random_,
                          common_config_);

    HostsPerLocalitySharedPtr upstream_per_zone_hosts =
        generateHostsPerZone(healthy_destination_cluster);
    HostsPerLocalitySharedPtr local_per_zone_hosts = generateHostsPerZone(originating_cluster);

    HostVectorSharedPtr originating_hosts = generateHostList(originating_cluster);
    HostVectorSharedPtr healthy_destination = generateHostList(healthy_destination_cluster);
    host_set_.healthy_hosts_ = *healthy_destination;
    HostVectorSharedPtr all_destination = generateHostList(all_destination_cluster);
    host_set_.hosts_ = *all_destination;

    std::map<std::string, uint32_t> hits;
    for (uint32_t i = 0; i < total_number_of_requests; ++i) {
      HostSharedPtr from_host = selectOriginatingHost(*originating_hosts);
      uint32_t from_zone = atoi(from_host->locality().zone().c_str());

      // Populate host set for upstream cluster.
      std::vector<HostVector> per_zone_upstream;
      per_zone_upstream.push_back(upstream_per_zone_hosts->get()[from_zone]);
      for (size_t zone = 0; zone < upstream_per_zone_hosts->get().size(); ++zone) {
        if (zone == from_zone) {
          continue;
        }

        per_zone_upstream.push_back(upstream_per_zone_hosts->get()[zone]);
      }
      auto per_zone_upstream_shared = makeHostsPerLocality(std::move(per_zone_upstream));
      host_set_.hosts_per_locality_ = per_zone_upstream_shared;
      host_set_.healthy_hosts_per_locality_ = per_zone_upstream_shared;

      // Populate host set for originating cluster.
      std::vector<HostVector> per_zone_local;
      per_zone_local.push_back(local_per_zone_hosts->get()[from_zone]);
      for (size_t zone = 0; zone < local_per_zone_hosts->get().size(); ++zone) {
        if (zone == from_zone) {
          continue;
        }

        per_zone_local.push_back(local_per_zone_hosts->get()[zone]);
      }
      auto per_zone_local_shared = makeHostsPerLocality(std::move(per_zone_local));
      local_priority_set_->updateHosts(
          0,
          updateHostsParams(originating_hosts, per_zone_local_shared,
                            std::make_shared<const HealthyHostVector>(*originating_hosts),
                            per_zone_local_shared),
          {}, empty_vector_, empty_vector_, random_.random(), absl::nullopt);

      HostConstSharedPtr selected = lb.chooseHost(nullptr);
      hits[selected->address()->asString()]++;
    }

    double mean = total_number_of_requests * 1.0 / hits.size();
    for (const auto& host_hit_num_pair : hits) {
      double percent_diff = std::abs((mean - host_hit_num_pair.second) / mean) * 100;
      std::cout << fmt::format("url:{}, hits:{}, {} % from mean", host_hit_num_pair.first,
                               host_hit_num_pair.second, percent_diff)
                << std::endl;
    }
  }

  HostSharedPtr selectOriginatingHost(const HostVector& hosts) {
    // Originating cluster should have roughly the same per host request distribution.
    return hosts[random_.random() % hosts.size()];
  }

  /**
   * Generate list of hosts based on number of hosts in the given zone.
   * @param hosts number of hosts per zone.
   */
  HostVectorSharedPtr generateHostList(const std::vector<uint32_t>& hosts) {
    HostVectorSharedPtr ret(new HostVector());
    for (size_t i = 0; i < hosts.size(); ++i) {
      const std::string zone = std::to_string(i);
      for (uint32_t j = 0; j < hosts[i]; ++j) {
        const std::string url = fmt::format("tcp://host.{}.{}:80", i, j);
        ret->push_back(newTestHost(info_, url, time_source_, 1, zone));
      }
    }

    return ret;
  }

  /**
   * Generate hosts by zone.
   * @param hosts number of hosts per zone.
   */
  HostsPerLocalitySharedPtr generateHostsPerZone(const std::vector<uint32_t>& hosts) {
    std::vector<HostVector> ret;
    for (size_t i = 0; i < hosts.size(); ++i) {
      const std::string zone = std::to_string(i);
      HostVector zone_hosts;

      for (uint32_t j = 0; j < hosts[i]; ++j) {
        const std::string url = fmt::format("tcp://host.{}.{}:80", i, j);
        zone_hosts.push_back(newTestHost(info_, url, time_source_, 1, zone));
      }

      ret.push_back(std::move(zone_hosts));
    }

    return makeHostsPerLocality(std::move(ret));
  };

  const uint32_t total_number_of_requests = 1000000;
  HostVector empty_vector_;

  PrioritySetImpl* local_priority_set_;
  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<MockTimeSystem> time_source_;
  Random::RandomGeneratorImpl random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterLbStatNames stat_names_;
  ClusterLbStats stats_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
};

TEST_F(DISABLED_SimulationTest, StrictlyEqualDistribution) {
  run({1U, 1U, 1U}, {3U, 3U, 3U}, {3U, 3U, 3U});
}

TEST_F(DISABLED_SimulationTest, UnequalZoneDistribution) {
  run({1U, 1U, 1U}, {2U, 5U, 5U}, {2U, 5U, 5U});
}

TEST_F(DISABLED_SimulationTest, UnequalZoneDistribution2) {
  run({1U, 1U, 1U}, {5U, 5U, 6U}, {5U, 5U, 6U});
}

TEST_F(DISABLED_SimulationTest, UnequalZoneDistribution3) {
  run({1U, 1U, 1U}, {10U, 10U, 10U}, {10U, 8U, 8U});
}

TEST_F(DISABLED_SimulationTest, UnequalZoneDistribution4) {
  run({20U, 20U, 21U}, {4U, 5U, 5U}, {4U, 5U, 5U});
}

TEST_F(DISABLED_SimulationTest, UnequalZoneDistribution5) {
  run({3U, 2U, 5U}, {4U, 5U, 5U}, {4U, 5U, 5U});
}

TEST_F(DISABLED_SimulationTest, UnequalZoneDistribution6) {
  run({3U, 2U, 5U}, {3U, 4U, 5U}, {3U, 4U, 5U});
}

} // namespace
} // namespace Upstream
} // namespace Envoy
