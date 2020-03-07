#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "common/common/fmt.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"
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
namespace {

static HostSharedPtr newTestHost(Upstream::ClusterInfoConstSharedPtr cluster,
                                 const std::string& url, uint32_t weight = 1,
                                 const std::string& zone = "") {
  envoy::config::core::v3::Locality locality;
  locality.set_zone(zone);
  return HostSharedPtr{
      new HostImpl(cluster, "", Network::Utility::resolveUrl(url), nullptr, weight, locality,
                   envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0,
                   envoy::config::core::v3::UNKNOWN)};
}

// Simulate weighted LR load balancer.
TEST(DISABLED_LeastRequestLoadBalancerWeightTest, Weight) {
  const uint64_t num_hosts = 4;
  const uint64_t weighted_subset_percent = 50;
  const uint64_t weight = 2;          // weighted_subset_percent of hosts will have this weight.
  const uint64_t active_requests = 3; // weighted_subset_percent will have this active requests.

  PrioritySetImpl priority_set;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  HostVector hosts;
  for (uint64_t i = 0; i < num_hosts; i++) {
    const bool should_weight = i < num_hosts * (weighted_subset_percent / 100.0);
    hosts.push_back(makeTestHost(info_, fmt::format("tcp://10.0.{}.{}:6379", i / 256, i % 256),
                                 should_weight ? weight : 1));
    if (should_weight) {
      hosts.back()->stats().rq_active_.set(active_requests);
    }
  }
  HostVectorConstSharedPtr updated_hosts{new HostVector(hosts)};
  HostsPerLocalitySharedPtr updated_locality_hosts{new HostsPerLocalityImpl(hosts)};
  priority_set.updateHosts(
      0,
      updateHostsParams(updated_hosts, updated_locality_hosts,
                        std::make_shared<const HealthyHostVector>(*updated_hosts),
                        updated_locality_hosts),
      {}, hosts, {}, absl::nullopt);

  Stats::IsolatedStoreImpl stats_store;
  ClusterStats stats{ClusterInfoImpl::generateStats(stats_store)};
  stats.max_host_weight_.set(weight);
  NiceMock<Runtime::MockLoader> runtime;
  Runtime::RandomGeneratorImpl random;
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig least_request_lb_config;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config;
  LeastRequestLoadBalancer lb_{
      priority_set, nullptr, stats, runtime, random, common_config, least_request_lb_config};

  std::unordered_map<HostConstSharedPtr, uint64_t> host_hits;
  const uint64_t total_requests = 100;
  for (uint64_t i = 0; i < total_requests; i++) {
    host_hits[lb_.chooseHost(nullptr)]++;
  }

  std::unordered_map<uint64_t, double> weight_to_percent;
  for (const auto& host : host_hits) {
    std::cout << fmt::format("url:{}, weight:{}, hits:{}, percent_of_total:{}\n",
                             host.first->address()->asString(), host.first->weight(), host.second,
                             (static_cast<double>(host.second) / total_requests) * 100);
    weight_to_percent[host.first->weight()] +=
        (static_cast<double>(host.second) / total_requests) * 100;
  }

  for (const auto& weight : weight_to_percent) {
    std::cout << fmt::format("weight:{}, percent:{}\n", weight.first, weight.second);
  }
}

/**
 * This test is for simulation only and should not be run as part of unit tests.
 */
class DISABLED_SimulationTest : public testing::Test {
public:
  DISABLED_SimulationTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {
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
          {}, empty_vector_, empty_vector_, absl::nullopt);

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
        ret->push_back(newTestHost(info_, url, 1, zone));
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
        zone_hosts.push_back(newTestHost(info_, url, 1, zone));
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
  Runtime::RandomGeneratorImpl random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
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
