#include "load_balancer_impl.h"

#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"

namespace Upstream {

const std::vector<HostPtr>& LoadBalancerBase::hostsToUse() {
  ASSERT(host_set_.healthyHosts().size() <= host_set_.hosts().size());
  if (host_set_.hosts().empty()) {
    return host_set_.hosts();
  }

  uint64_t global_panic_threshold =
      std::min(100UL, runtime_.snapshot().getInteger("upstream.healthy_panic_threshold", 50));
  double healthy_percent =
      (host_set_.healthyHosts().size() / static_cast<double>(host_set_.hosts().size())) * 100;

  // If the % of healthy hosts in the cluster is less than our panic threshold, we use all hosts.
  if (healthy_percent < global_panic_threshold) {
    stats_.upstream_rq_lb_healthy_panic_.inc();
    return host_set_.hosts();
  }

  // Attempt to do zone aware routing if there are at least 2 upstream zones and it's enabled.
  if (stats_.upstream_zone_count_.value() > 1 &&
      runtime_.snapshot().featureEnabled("upstream.zone_routing.enabled", 100)) {
    double zone_to_all_percent =
        100.0 * host_set_.localZoneHealthyHosts().size() / host_set_.healthyHosts().size();
    double expected_percent = 100.0 / stats_.upstream_zone_count_.value();

    uint64_t zone_percent_diff =
        runtime_.snapshot().getInteger("upstream.zone_routing.percent_diff", 3);

    // Hosts should be roughly equally distributed between zones.
    if (std::abs(zone_to_all_percent - expected_percent) > zone_percent_diff) {
      stats_.upstream_zone_above_threshold_.inc();

      return host_set_.healthyHosts();
    } else {
      stats_.upstream_zone_within_threshold_.inc();
    }

    uint64_t zone_panic_threshold =
        runtime_.snapshot().getInteger("upstream.zone_routing.healthy_panic_threshold", 80);
    double zone_healthy_percent =
        100.0 * host_set_.localZoneHealthyHosts().size() / host_set_.localZoneHosts().size();
    if (zone_healthy_percent < zone_panic_threshold) {
      stats_.upstream_zone_healthy_panic_.inc();

      return host_set_.healthyHosts();
    }

    return host_set_.localZoneHealthyHosts();
  }

  return host_set_.healthyHosts();
}

ConstHostPtr RoundRobinLoadBalancer::chooseHost() {
  const std::vector<HostPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  return hosts_to_use[rr_index_++ % hosts_to_use.size()];
}

LeastRequestLoadBalancer::LeastRequestLoadBalancer(const HostSet& host_set, ClusterStats& stats,
                                                   Runtime::Loader& runtime,
                                                   Runtime::RandomGenerator& random)
    : LoadBalancerBase(host_set, stats, runtime), random_(random) {
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
