#include "load_balancer_impl.h"

#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"

namespace Upstream {

bool LoadBalancerBase::earlyExitNonZoneRouting() {
  uint32_t number_of_zones = host_set_.healthyHostsPerZone().size();
  if (number_of_zones < 2 ||
      !runtime_.snapshot().featureEnabled("upstream.zone_routing.enabled", 100)) {
    return true;
  }

  const std::vector<HostPtr>& local_zone_healthy_hosts = host_set_.healthyHostsPerZone()[0];
  if (local_zone_healthy_hosts.empty()) {
    return true;
  }

  // Do not perform zone routing for small clusters.
  uint64_t min_cluster_size =
      runtime_.snapshot().getInteger("upstream.zone_routing.min_cluster_size", 6U);

  if (host_set_.healthyHosts().size() < min_cluster_size) {
    stats_.zone_cluster_too_small_.inc();
    return true;
  }

  // If local cluster is not set, or we are in panic mode for it.
  if (local_host_set_ == nullptr || local_host_set_->hosts().empty() ||
      isGlobalPanic(*local_host_set_)) {
    stats_.local_cluster_not_ok_.inc();
    return true;
  }

  // Same number of zones should be for local and upstream cluster.
  if (host_set_.healthyHostsPerZone().size() != local_host_set_->healthyHostsPerZone().size()) {
    stats_.zone_number_differs_.inc();
    return true;
  }

  return false;
}

bool LoadBalancerBase::isGlobalPanic(const HostSet& host_set) {
  uint64_t global_panic_threshold =
      std::min(100UL, runtime_.snapshot().getInteger("upstream.healthy_panic_threshold", 50));
  double healthy_percent = 100.0 * host_set.healthyHosts().size() / host_set.hosts().size();

  // If the % of healthy hosts in the cluster is less than our panic threshold, we use all hosts.
  if (healthy_percent < global_panic_threshold) {
    stats_.upstream_rq_lb_healthy_panic_.inc();
    return true;
  }

  return false;
}

std::vector<uint64_t>
LoadBalancerBase::calculateZonePercentage(const std::vector<std::vector<HostPtr>>& hosts_per_zone) {
  std::vector<uint64_t> percentage(hosts_per_zone.size());

  uint64_t total_hosts = 0;
  for (const auto& zone_hosts : hosts_per_zone) {
    total_hosts += zone_hosts.size();
  }

  if (total_hosts != 0) {
    size_t pos = 0;
    for (const auto& zone_hosts : hosts_per_zone) {
      percentage[pos++] = 10000ULL * zone_hosts.size() / total_hosts;
    }
  }

  return percentage;
}

const std::vector<HostPtr>& LoadBalancerBase::tryChooseLocalZoneHosts() {
  // At this point it's guaranteed to be at least 2 zones.
  ASSERT(host_set_.healthyHostsPerZone().size() >= 2U);

  std::vector<uint64_t> local_percentage =
      calculateZonePercentage(local_host_set_->healthyHostsPerZone());
  std::vector<uint64_t> upstream_percentage =
      calculateZonePercentage(host_set_.healthyHostsPerZone());

  // Try to push all of the requests to the same zone first.
  // If we have lower percent of hosts in the local cluster in the same zone,
  // we can push all of the requests directly to upstream cluster in the same zone.
  if (upstream_percentage[0] >= local_percentage[0]) {
    stats_.zone_over_percentage_.inc();
    return host_set_.healthyHostsPerZone()[0];
  }

  // If we cannot route all requests to the same zone, calculate what percentage can be routed.
  // For example, if local percentage is 20% and upstream is 10%
  // we can route only 50% of requests directly.
  uint64_t local_percent_route = upstream_percentage[0] * 10000 / local_percentage[0];
  if (random_.random() % 10000 < local_percent_route) {
    stats_.zone_routing_sampled_.inc();
    return host_set_.healthyHostsPerZone()[0];
  }

  // At this point we should route cross zone as we cannot route to the local zone.
  stats_.zone_routing_no_sampled_.inc();

  std::vector<uint64_t> capacity_left;
  // Local zone does not have additional capacity (we already routed what we could), but
  // put it to the capacity_left so that index in the array matches to the zone index.
  capacity_left.push_back(0);
  for (size_t i = 1; i < local_percentage.size(); ++i) {
    // Only route to the zones that have additional capacity.
    if (upstream_percentage[i] > local_percentage[i]) {
      capacity_left.push_back(capacity_left[i - 1] + upstream_percentage[i] - local_percentage[i]);
    } else {
      capacity_left.push_back(capacity_left[i - 1]);
    }
  }

  // Select specific zone for cross zone traffic based on the additional capacity in zones.
  uint64_t threshold = random_.random() % capacity_left.back();

  // This potentially can be optimized to be O(log(N)) where N is the number of zones.
  // Linear scan should be faster for smaller N, in most of the scenarios N will be small.
  int pos = 0;
  while (threshold > capacity_left[pos]) {
    pos++;
  }

  return host_set_.healthyHostsPerZone()[pos];
}

const std::vector<HostPtr>& LoadBalancerBase::hostsToUse() {
  ASSERT(host_set_.healthyHosts().size() <= host_set_.hosts().size());

  if (host_set_.hosts().empty() || isGlobalPanic(host_set_)) {
    return host_set_.hosts();
  }

  if (earlyExitNonZoneRouting()) {
    return host_set_.healthyHosts();
  }

  return tryChooseLocalZoneHosts();
}

ConstHostPtr RoundRobinLoadBalancer::chooseHost() {
  const std::vector<HostPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  return hosts_to_use[rr_index_++ % hosts_to_use.size()];
}

LeastRequestLoadBalancer::LeastRequestLoadBalancer(const HostSet& host_set,
                                                   const HostSet* local_host_set,
                                                   ClusterStats& stats, Runtime::Loader& runtime,
                                                   Runtime::RandomGenerator& random)
    : LoadBalancerBase(host_set, local_host_set, stats, runtime, random) {
  host_set.addMemberUpdateCb(
      [this](const std::vector<HostPtr>&, const std::vector<HostPtr>& hosts_removed) -> void {
        if (last_host_) {
          for (const HostPtr& host : hosts_removed) {
            if (host == last_host_) {
              hits_left_ = 0;
              last_host_.reset();

              break;
            }
          }
        }
      });
}

ConstHostPtr LeastRequestLoadBalancer::chooseHost() {
  bool is_weight_imbalanced = stats_.max_host_weight_.value() != 1;
  bool is_weight_enabled = runtime_.snapshot().getInteger("upstream.weight_enabled", 1UL) != 0;

  if (is_weight_imbalanced && hits_left_ > 0 && is_weight_enabled) {
    --hits_left_;

    return last_host_;
  } else {
    // To avoid hit stale last_host_ when all hosts become weight balanced.
    hits_left_ = 0;
    last_host_.reset();
  }

  const std::vector<HostPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  // Make weighed random if we have hosts with non 1 weights.
  if (is_weight_imbalanced & is_weight_enabled) {
    last_host_ = hosts_to_use[random_.random() % hosts_to_use.size()];
    hits_left_ = last_host_->weight() - 1;

    return last_host_;
  } else {
    HostPtr host1 = hosts_to_use[random_.random() % hosts_to_use.size()];
    HostPtr host2 = hosts_to_use[random_.random() % hosts_to_use.size()];
    if (host1->stats().rq_active_.value() < host2->stats().rq_active_.value()) {
      return host1;
    } else {
      return host2;
    }
  }
}

ConstHostPtr RandomLoadBalancer::chooseHost() {
  const std::vector<HostPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  return hosts_to_use[random_.random() % hosts_to_use.size()];
}

} // Upstream
