#include "common/runtime/runtime_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::NiceMock;
using testing::Return;

namespace Upstream {

static HostPtr newTestHost(const Upstream::Cluster& cluster, const std::string& url,
                           uint32_t weight = 1, const std::string& zone = "") {
  return HostPtr{new HostImpl(cluster, url, false, weight, zone)};
}

/**
 * This test is for simulation only and should not be run as part of unit tests.
 */
class DISABLED_SimulationTest : public testing::Test {
public:
  DISABLED_SimulationTest() : stats_(ClusterImplBase::generateStats("", stats_store_)) {
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
    stats_.upstream_zone_count_.set(all_destination_cluster.size());

    std::unordered_map<std::string, std::vector<HostPtr>> healthy_map =
        generateHostsPerZone(healthy_destination_cluster);
    std::unordered_map<std::string, std::vector<HostPtr>> all_map =
        generateHostsPerZone(all_destination_cluster);

    std::vector<HostPtr> originating_hosts = generateHostList(originating_cluster);
    cluster_.healthy_hosts_ = generateHostList(healthy_destination_cluster);
    cluster_.hosts_ = generateHostList(all_destination_cluster);

    std::map<std::string, uint32_t> hits;
    for (uint32_t i = 0; i < total_number_of_requests; ++i) {
      HostPtr from_host = selectOriginatingHost(originating_hosts);

      cluster_.local_zone_hosts_ = all_map[from_host->zone()];
      cluster_.local_zone_healthy_hosts_ = healthy_map[from_host->zone()];

      ConstHostPtr selected = lb_.chooseHost();
      hits[selected->url()]++;
    }

    for (const auto& host_hit_num_pair : hits) {
      std::cout << fmt::format("url:{}, hits:{}", host_hit_num_pair.first, host_hit_num_pair.second)
                << std::endl;
    }
  }

  HostPtr selectOriginatingHost(const std::vector<HostPtr>& hosts) {
    // Originating cluster should have roughly the same per host request distribution.
    return hosts[random_.random() % hosts.size()];
  }

  /**
   * Generate list of hosts based on number of hosts in the given zone.
   * @param hosts number of hosts per zone.
   */
  std::vector<HostPtr> generateHostList(const std::vector<uint32_t>& hosts) {
    std::vector<HostPtr> ret;
    for (size_t i = 0; i < hosts.size(); ++i) {
      const std::string zone = std::to_string(i);
      for (uint32_t j = 0; j < hosts[i]; ++j) {
        const std::string url = fmt::format("tcp://host.{}.{}:80", i, j);
        ret.push_back(newTestHost(cluster_, url, 1, zone));
      }
    }

    return ret;
  }

  /**
   * Generate hosts by zone.
   * @param hosts number of hosts per zone.
   */
  std::unordered_map<std::string, std::vector<HostPtr>>
  generateHostsPerZone(const std::vector<uint32_t>& hosts) {
    std::unordered_map<std::string, std::vector<HostPtr>> ret;
    for (size_t i = 0; i < hosts.size(); ++i) {
      const std::string zone = std::to_string(i);
      std::vector<HostPtr> zone_hosts;

      for (uint32_t j = 0; j < hosts[i]; ++j) {
        const std::string url = fmt::format("tcp://host.{}.{}:80", i, j);
        zone_hosts.push_back(newTestHost(cluster_, url, 1, zone));
      }

      ret.insert({zone, std::move(zone_hosts)});
    }

    return ret;
  };

  const uint32_t total_number_of_requests = 3000000;

  NiceMock<MockCluster> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  Runtime::RandomGeneratorImpl random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  // TODO: make per originating host load balancer.
  RandomLoadBalancer lb_{cluster_, nullptr, stats_, runtime_, random_};
};

TEST_F(DISABLED_SimulationTest, strictlyEqualDistribution) {
  run({1U, 1U, 1U}, {3U, 3U, 3U}, {3U, 3U, 3U});
}

TEST_F(DISABLED_SimulationTest, unequalZoneDistribution) {
  run({1U, 1U, 1U}, {2U, 5U, 5U}, {2U, 5U, 5U});
}

TEST_F(DISABLED_SimulationTest, unequalZoneDistribution2) {
  run({1U, 1U, 1U}, {5U, 5U, 6U}, {5U, 5U, 6U});
}

TEST_F(DISABLED_SimulationTest, unequalZoneDistribution3) {
  run({1U, 1U, 1U}, {10U, 10U, 10U}, {10U, 8U, 8U});
}

TEST_F(DISABLED_SimulationTest, unequalZoneDistribution4) {
  run({20U, 20U, 21U}, {4U, 4U, 5U}, {4U, 5U, 5U});
}

} // Upstream