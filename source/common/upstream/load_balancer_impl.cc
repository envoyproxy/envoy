#include "common/upstream/load_balancer_impl.h"

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Upstream {
namespace {

const HostSet* bestAvailable(const PrioritySet* priority_set) {
  if (priority_set == nullptr) {
    return nullptr;
  }
  for (auto& host_set : priority_set->hostSetsPerPriority()) {
    if (!host_set->healthyHosts().empty()) {
      return host_set.get();
    }
  }
  return priority_set->hostSetsPerPriority()[0].get();
}

} // namespace

static const std::string RuntimeZoneEnabled = "upstream.zone_routing.enabled";
static const std::string RuntimeMinClusterSize = "upstream.zone_routing.min_cluster_size";
static const std::string RuntimePanicThreshold = "upstream.healthy_panic_threshold";

LoadBalancerBase::LoadBalancerBase(const PrioritySet& priority_set,
                                   const PrioritySet* local_priority_set, ClusterStats& stats,
                                   Runtime::Loader& runtime, Runtime::RandomGenerator& random)
    : stats_(stats), runtime_(runtime), random_(random), priority_set_(priority_set),
      best_available_host_set_(bestAvailable(&priority_set)),
      local_priority_set_(local_priority_set) {
  resizePerPriorityState(priority_set.hostSetsPerPriority().size());
  priority_set_.addMemberUpdateCb([this](uint32_t priority, const std::vector<HostSharedPtr>&,
                                         const std::vector<HostSharedPtr>&) -> void {
    // Update the host set to use for picking, based on the new state.
    best_available_host_set_ = bestAvailable(&priority_set_);
    // If there's a local priority set, regenerate all routing.  If not, make
    // sure per_priority_state_ is still large enough to not cause problems.
    if (local_priority_set_) {
      regenerateLocalityRoutingStructures(priority);
    } else {
      resizePerPriorityState(priority_set_.hostSetsPerPriority().size());
    }
  });
  if (local_priority_set_) {
    // Multiple priorities are unsupported for local priority sets.
    // TODO(alysswilk) find the right place to reject config with this.
    ASSERT(local_priority_set_->hostSetsPerPriority().size() == 1);
    local_priority_set_member_update_cb_handle_ = local_priority_set_->addMemberUpdateCb(
        [this](uint32_t priority, const std::vector<HostSharedPtr>&,
               const std::vector<HostSharedPtr>&) -> void {
          ASSERT(priority == 0);
          regenerateLocalityRoutingStructures(best_available_priority());
        });
  }
}

LoadBalancerBase::~LoadBalancerBase() {
  if (local_priority_set_member_update_cb_handle_ != nullptr) {
    local_priority_set_member_update_cb_handle_->remove();
  }
}

void LoadBalancerBase::regenerateLocalityRoutingStructures(uint32_t priority) {
  ASSERT(local_priority_set_);
  stats_.lb_recalculate_zone_structures_.inc();
  resizePerPriorityState(priority + 1);

  // Do not perform any calculations if we cannot perform locality routing based on non runtime
  // params.
  PerPriorityState& state = *per_priority_state_[priority];
  if (earlyExitNonLocalityRouting(priority)) {
    state.locality_routing_state_ = LocalityRoutingState::NoLocalityRouting;
    return;
  }
  HostSet& host_set = *priority_set_.hostSetsPerPriority()[priority];
  size_t num_localities = host_set.healthyHostsPerLocality().size();
  ASSERT(num_localities > 0);

  uint64_t local_percentage[num_localities];
  calculateLocalityPercentage(local_host_set().healthyHostsPerLocality(), local_percentage);

  uint64_t upstream_percentage[num_localities];
  calculateLocalityPercentage(host_set.healthyHostsPerLocality(), upstream_percentage);

  // If we have lower percent of hosts in the local cluster in the same locality,
  // we can push all of the requests directly to upstream cluster in the same locality.
  if (upstream_percentage[0] >= local_percentage[0]) {
    state.locality_routing_state_ = LocalityRoutingState::LocalityDirect;
    return;
  }

  state.locality_routing_state_ = LocalityRoutingState::LocalityResidual;

  // If we cannot route all requests to the same locality, calculate what percentage can be routed.
  // For example, if local percentage is 20% and upstream is 10%
  // we can route only 50% of requests directly.
  state.local_percent_to_route_ = upstream_percentage[0] * 10000 / local_percentage[0];

  // Local locality does not have additional capacity (we have already routed what we could).
  // Now we need to figure out how much traffic we can route cross locality and to which exact
  // locality we should route. Percentage of requests routed cross locality to a specific locality
  // needed be proportional to the residual capacity upstream locality has.
  //
  // residual_capacity contains capacity left in a given locality, we keep accumulating residual
  // capacity to make search for sampled value easier.
  // For example, if we have the following upstream and local percentage:
  // local_percentage: 40000 40000 20000
  // upstream_percentage: 25000 50000 25000
  // Residual capacity would look like: 0 10000 5000. Now we need to sample proportionally to
  // bucket sizes (residual capacity). For simplicity of finding where specific
  // sampled value is, we accumulate values in residual capacity. This is what it will look like:
  // residual_capacity: 0 10000 15000
  // Now to find a locality to route (bucket) we could simply iterate over residual_capacity
  // searching where sampled value is placed.
  state.residual_capacity_.resize(num_localities);

  // Local locality (index 0) does not have residual capacity as we have routed all we could.
  state.residual_capacity_[0] = 0;
  for (size_t i = 1; i < num_localities; ++i) {
    // Only route to the localities that have additional capacity.
    if (upstream_percentage[i] > local_percentage[i]) {
      state.residual_capacity_[i] =
          state.residual_capacity_[i - 1] + upstream_percentage[i] - local_percentage[i];
    } else {
      // Locality with index "i" does not have residual capacity, but we keep accumulating previous
      // values to make search easier on the next step.
      state.residual_capacity_[i] = state.residual_capacity_[i - 1];
    }
  }
};

void LoadBalancerBase::resizePerPriorityState(uint32_t size) {
  size = std::max<uint32_t>(size, priority_set_.hostSetsPerPriority().size());
  if (per_priority_state_.size() < size) {
    for (size_t i = 0; i < size - per_priority_state_.size() + 1; ++i) {
      per_priority_state_.push_back(PerPriorityStatePtr{new PerPriorityState});
    }
  }
}

bool LoadBalancerBase::earlyExitNonLocalityRouting(uint32_t priority) {
  if (priority_set_.hostSetsPerPriority().size() < priority + 1) {
    return true;
  }

  HostSet& host_set = *priority_set_.hostSetsPerPriority()[priority];
  if (host_set.healthyHostsPerLocality().size() < 2) {
    return true;
  }

  if (host_set.healthyHostsPerLocality()[0].empty()) {
    return true;
  }

  // Same number of localities should be for local and upstream cluster.
  if (host_set.healthyHostsPerLocality().size() !=
      local_host_set().healthyHostsPerLocality().size()) {
    stats_.lb_zone_number_differs_.inc();
    return true;
  }

  // Do not perform locality routing for small clusters.
  uint64_t min_cluster_size = runtime_.snapshot().getInteger(RuntimeMinClusterSize, 6U);
  if (host_set.healthyHosts().size() < min_cluster_size) {
    stats_.lb_zone_cluster_too_small_.inc();
    return true;
  }

  return false;
}

bool LoadBalancerUtility::isGlobalPanic(const HostSet& host_set, Runtime::Loader& runtime) {
  uint64_t global_panic_threshold =
      std::min<uint64_t>(100, runtime.snapshot().getInteger(RuntimePanicThreshold, 50));
  double healthy_percent = host_set.hosts().size() == 0
                               ? 0
                               : 100.0 * host_set.healthyHosts().size() / host_set.hosts().size();

  // If the % of healthy hosts in the cluster is less than our panic threshold, we use all hosts.
  if (healthy_percent < global_panic_threshold) {
    return true;
  }

  return false;
}

void LoadBalancerBase::calculateLocalityPercentage(
    const std::vector<std::vector<HostSharedPtr>>& hosts_per_locality, uint64_t* ret) {
  uint64_t total_hosts = 0;
  for (const auto& locality_hosts : hosts_per_locality) {
    total_hosts += locality_hosts.size();
  }

  size_t i = 0;
  for (const auto& locality_hosts : hosts_per_locality) {
    ret[i++] = total_hosts > 0 ? 10000ULL * locality_hosts.size() / total_hosts : 0;
  }
}

const std::vector<HostSharedPtr>& LoadBalancerBase::tryChooseLocalLocalityHosts() {
  PerPriorityState& state = *per_priority_state_[best_available_priority()];
  ASSERT(state.locality_routing_state_ != LocalityRoutingState::NoLocalityRouting);

  // At this point it's guaranteed to be at least 2 localities.
  size_t number_of_localities = best_available_host_set_->healthyHostsPerLocality().size();

  ASSERT(number_of_localities >= 2U);

  // Try to push all of the requests to the same locality first.
  if (state.locality_routing_state_ == LocalityRoutingState::LocalityDirect) {
    stats_.lb_zone_routing_all_directly_.inc();
    return best_available_host_set_->healthyHostsPerLocality()[0];
  }

  ASSERT(state.locality_routing_state_ == LocalityRoutingState::LocalityResidual);

  // If we cannot route all requests to the same locality, we already calculated how much we can
  // push to the local locality, check if we can push to local locality on current iteration.
  if (random_.random() % 10000 < state.local_percent_to_route_) {
    stats_.lb_zone_routing_sampled_.inc();
    return best_available_host_set_->healthyHostsPerLocality()[0];
  }

  // At this point we must route cross locality as we cannot route to the local locality.
  stats_.lb_zone_routing_cross_zone_.inc();

  // This is *extremely* unlikely but possible due to rounding errors when calculating
  // locality percentages. In this case just select random locality.
  if (state.residual_capacity_[number_of_localities - 1] == 0) {
    stats_.lb_zone_no_capacity_left_.inc();
    return best_available_host_set_
        ->healthyHostsPerLocality()[random_.random() % number_of_localities];
  }

  // Random sampling to select specific locality for cross locality traffic based on the additional
  // capacity in localities.
  uint64_t threshold = random_.random() % state.residual_capacity_[number_of_localities - 1];

  // This potentially can be optimized to be O(log(N)) where N is the number of localities.
  // Linear scan should be faster for smaller N, in most of the scenarios N will be small.
  int i = 0;
  while (threshold > state.residual_capacity_[i]) {
    i++;
  }

  return best_available_host_set_->healthyHostsPerLocality()[i];
}

const std::vector<HostSharedPtr>& LoadBalancerBase::hostsToUse() {
  ASSERT(best_available_host_set_->healthyHosts().size() <=
         best_available_host_set_->hosts().size());

  // If the best available priority has insufficient healthy hosts, return all hosts.
  if (LoadBalancerUtility::isGlobalPanic(*best_available_host_set_, runtime_)) {
    stats_.lb_healthy_panic_.inc();
    return best_available_host_set_->hosts();
  }

  // If we've latched that we can't do priority-based routing, return healthy
  // hosts for the best available priority.
  if (per_priority_state_[best_available_priority()]->locality_routing_state_ ==
      LocalityRoutingState::NoLocalityRouting) {
    return best_available_host_set_->healthyHosts();
  }

  // Determine if the load balancer should do zone based routing for this pick.
  if (!runtime_.snapshot().featureEnabled(RuntimeZoneEnabled, 100)) {
    return best_available_host_set_->healthyHosts();
  }

  if (LoadBalancerUtility::isGlobalPanic(local_host_set(), runtime_)) {
    stats_.lb_local_cluster_not_ok_.inc();
    // If the local Envoy instances are in global panic, do not do locality
    // based routing.
    return best_available_host_set_->healthyHosts();
  }

  return tryChooseLocalLocalityHosts();
}

HostConstSharedPtr RoundRobinLoadBalancer::chooseHost(LoadBalancerContext*) {
  const std::vector<HostSharedPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  return hosts_to_use[rr_index_++ % hosts_to_use.size()];
}

LeastRequestLoadBalancer::LeastRequestLoadBalancer(const PrioritySet& priority_set,
                                                   const PrioritySet* local_priority_set,
                                                   ClusterStats& stats, Runtime::Loader& runtime,
                                                   Runtime::RandomGenerator& random)
    : LoadBalancerBase(priority_set, local_priority_set, stats, runtime, random) {
  priority_set.addMemberUpdateCb([this](uint32_t, const std::vector<HostSharedPtr>&,
                                        const std::vector<HostSharedPtr>& hosts_removed) -> void {
    if (last_host_) {
      for (const HostSharedPtr& host : hosts_removed) {
        if (host == last_host_) {
          hits_left_ = 0;
          last_host_.reset();

          break;
        }
      }
    }
  });
}

HostConstSharedPtr LeastRequestLoadBalancer::chooseHost(LoadBalancerContext*) {
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

  const std::vector<HostSharedPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  // Make weighed random if we have hosts with non 1 weights.
  if (is_weight_imbalanced & is_weight_enabled) {
    last_host_ = hosts_to_use[random_.random() % hosts_to_use.size()];
    hits_left_ = last_host_->weight() - 1;

    return last_host_;
  } else {
    HostSharedPtr host1 = hosts_to_use[random_.random() % hosts_to_use.size()];
    HostSharedPtr host2 = hosts_to_use[random_.random() % hosts_to_use.size()];
    if (host1->stats().rq_active_.value() < host2->stats().rq_active_.value()) {
      return host1;
    } else {
      return host2;
    }
  }
}

HostConstSharedPtr RandomLoadBalancer::chooseHost(LoadBalancerContext*) {
  const std::vector<HostSharedPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  return hosts_to_use[random_.random() % hosts_to_use.size()];
}

} // namespace Upstream
} // namespace Envoy
