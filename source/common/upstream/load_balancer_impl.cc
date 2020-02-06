#include "common/upstream/load_balancer_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/protobuf/utility.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Upstream {

namespace {
static const std::string RuntimeZoneEnabled = "upstream.zone_routing.enabled";
static const std::string RuntimeMinClusterSize = "upstream.zone_routing.min_cluster_size";
static const std::string RuntimePanicThreshold = "upstream.healthy_panic_threshold";

// Distributes load between priorities based on the per priority availability and the normalized
// total availability. Load is assigned to each priority according to how available each priority is
// adjusted for the normalized total availability.
//
// @param per_priority_load vector of loads that should be populated.
// @param per_priority_availability the percentage availability of each priority, used to determine
// how much load each priority can handle.
// @param total_load the amount of load that may be distributed. Will be updated with the amount of
// load remaining after distribution.
// @param normalized_total_availability the total availability, up to a max of 100. Used to
// scale the load when the total availability is less than 100%.
// @return the first available priority and the remaining load
std::pair<int32_t, size_t> distributeLoad(PriorityLoad& per_priority_load,
                                          const PriorityAvailability& per_priority_availability,
                                          size_t total_load, size_t normalized_total_availability) {
  int32_t first_available_priority = -1;
  for (size_t i = 0; i < per_priority_availability.get().size(); ++i) {
    if (first_available_priority < 0 && per_priority_availability.get()[i] > 0) {
      first_available_priority = i;
    }
    // Now assign as much load as possible to the high priority levels and cease assigning load
    // when total_load runs out.
    per_priority_load.get()[i] = std::min<uint32_t>(
        total_load, per_priority_availability.get()[i] * 100 / normalized_total_availability);
    total_load -= per_priority_load.get()[i];
  }

  return {first_available_priority, total_load};
}

// Returns true if the weights of all the hosts in the HostVector are equal.
bool hostWeightsAreEqual(const HostVector& hosts) {
  if (hosts.size() <= 1) {
    return true;
  }
  const uint32_t weight = hosts[0]->weight();
  for (size_t i = 1; i < hosts.size(); ++i) {
    if (hosts[i]->weight() != weight) {
      return false;
    }
  }
  return true;
}

} // namespace

std::pair<uint32_t, LoadBalancerBase::HostAvailability>
LoadBalancerBase::choosePriority(uint64_t hash, const HealthyLoad& healthy_per_priority_load,
                                 const DegradedLoad& degraded_per_priority_load) {
  hash = hash % 100 + 1; // 1-100
  uint32_t aggregate_percentage_load = 0;
  // As with tryChooseLocalLocalityHosts, this can be refactored for efficiency
  // but O(N) is good enough for now given the expected number of priorities is
  // small.

  // We first attempt to select a priority based on healthy availability.
  for (size_t priority = 0; priority < healthy_per_priority_load.get().size(); ++priority) {
    aggregate_percentage_load += healthy_per_priority_load.get()[priority];
    if (hash <= aggregate_percentage_load) {
      return {static_cast<uint32_t>(priority), HostAvailability::Healthy};
    }
  }

  // If no priorities were selected due to health, we'll select a priority based degraded
  // availability.
  for (size_t priority = 0; priority < degraded_per_priority_load.get().size(); ++priority) {
    aggregate_percentage_load += degraded_per_priority_load.get()[priority];
    if (hash <= aggregate_percentage_load) {
      return {static_cast<uint32_t>(priority), HostAvailability::Degraded};
    }
  }

  // The percentages should always add up to 100 but we have to have a return for the compiler.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

LoadBalancerBase::LoadBalancerBase(
    const PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : stats_(stats), runtime_(runtime), random_(random),
      default_healthy_panic_percent_(PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
          common_config, healthy_panic_threshold, 100, 50)),
      priority_set_(priority_set) {
  for (auto& host_set : priority_set_.hostSetsPerPriority()) {
    recalculatePerPriorityState(host_set->priority(), priority_set_, per_priority_load_,
                                per_priority_health_, per_priority_degraded_);
  }
  // Recalculate panic mode for all levels.
  recalculatePerPriorityPanic();

  priority_set_.addPriorityUpdateCb(
      [this](uint32_t priority, const HostVector&, const HostVector&) -> void {
        recalculatePerPriorityState(priority, priority_set_, per_priority_load_,
                                    per_priority_health_, per_priority_degraded_);
      });

  priority_set_.addPriorityUpdateCb(
      [this](uint32_t priority, const HostVector&, const HostVector&) -> void {
        UNREFERENCED_PARAMETER(priority);
        recalculatePerPriorityPanic();
      });
}

// The following cases are handled by
// recalculatePerPriorityState and recalculatePerPriorityPanic methods (normalized total health is
// sum of all priorities' health values and capped at 100).
// - normalized total health is = 100%. It means there are enough healthy hosts to handle the load.
//   Do not enter panic mode, even if a specific priority has low number of healthy hosts.
// - normalized total health is < 100%. There are not enough healthy hosts to handle the load.
// Continue distributing the load among priority sets, but turn on panic mode for a given priority
//   if # of healthy hosts in priority set is low.
// - all host sets are in panic mode. Situation called TotalPanic. Load distribution is
//   calculated based on the number of hosts in each priority regardless of their health.
// - all hosts in all priorities are down (normalized total health is 0%). If panic
//   threshold > 0% the cluster is in TotalPanic (see above). If panic threshold == 0
//   then priorities are not in panic, but there are no healthy hosts to route to.
//   In this case just mark P=0 as recipient of 100% of the traffic (nothing will be routed
//   to P=0 anyways as there are no healthy hosts there).
void LoadBalancerBase::recalculatePerPriorityState(uint32_t priority,
                                                   const PrioritySet& priority_set,
                                                   HealthyAndDegradedLoad& per_priority_load,
                                                   HealthyAvailability& per_priority_health,
                                                   DegradedAvailability& per_priority_degraded) {
  per_priority_load.healthy_priority_load_.get().resize(priority_set.hostSetsPerPriority().size());
  per_priority_load.degraded_priority_load_.get().resize(priority_set.hostSetsPerPriority().size());
  per_priority_health.get().resize(priority_set.hostSetsPerPriority().size());
  per_priority_degraded.get().resize(priority_set.hostSetsPerPriority().size());

  // Determine the health of the newly modified priority level.
  // Health ranges from 0-100, and is the ratio of healthy/degraded hosts to total hosts, modified
  // by the overprovisioning factor.
  HostSet& host_set = *priority_set.hostSetsPerPriority()[priority];
  per_priority_health.get()[priority] = 0;
  per_priority_degraded.get()[priority] = 0;
  const auto host_count = host_set.hosts().size() - host_set.excludedHosts().size();

  if (host_count > 0) {
    // Each priority level's health is ratio of healthy hosts to total number of hosts in a priority
    // multiplied by overprovisioning factor of 1.4 and capped at 100%. It means that if all
    // hosts are healthy that priority's health is 100%*1.4=140% and is capped at 100% which results
    // in 100%. If 80% of hosts are healthy, that priority's health is still 100% (80%*1.4=112% and
    // capped at 100%).
    per_priority_health.get()[priority] = std::min<uint32_t>(
        100, (host_set.overprovisioningFactor() * host_set.healthyHosts().size() / host_count));

    // We perform the same computation for degraded hosts.
    per_priority_degraded.get()[priority] = std::min<uint32_t>(
        100, (host_set.overprovisioningFactor() * host_set.degradedHosts().size() / host_count));
  }

  // Now that we've updated health for the changed priority level, we need to calculate percentage
  // load for all priority levels.

  // First, determine if the load needs to be scaled relative to availability (healthy + degraded).
  // For example if there are 3 host sets with 10% / 20% / 10% health and 20% / 10% / 0% degraded
  // they will get 16% / 28% / 14% load to healthy hosts and 28% / 14% / 0% load to degraded hosts
  // to ensure total load adds up to 100. Note the first healthy priority is receiving 2% additional
  // load due to rounding.
  //
  // Sum of priority levels' health and degraded values may exceed 100, so it is capped at 100 and
  // referred as normalized total availability.
  const uint32_t normalized_total_availability =
      calculateNormalizedTotalAvailability(per_priority_health, per_priority_degraded);
  if (normalized_total_availability == 0) {
    // Everything is terrible. There is nothing to calculate here.
    // Let recalculatePerPriorityPanic and recalculateLoadInTotalPanic deal with
    // load calculation.
    return;
  }

  // We start of with a total load of 100 and distribute it between priorities based on
  // availability. We first attempt to distribute this load to healthy priorities based on healthy
  // availability.
  const auto first_healthy_and_remaining =
      distributeLoad(per_priority_load.healthy_priority_load_, per_priority_health, 100,
                     normalized_total_availability);

  // Using the remaining load after allocating load to healthy priorities, distribute it based on
  // degraded availability.
  const auto remaining_load_for_degraded = first_healthy_and_remaining.second;
  const auto first_degraded_and_remaining =
      distributeLoad(per_priority_load.degraded_priority_load_, per_priority_degraded,
                     remaining_load_for_degraded, normalized_total_availability);

  // Anything that remains should just be rounding errors, so allocate that to the first available
  // priority, either as healthy or degraded.
  const auto remaining_load = first_degraded_and_remaining.second;
  if (remaining_load != 0) {
    const auto first_healthy = first_healthy_and_remaining.first;
    const auto first_degraded = first_degraded_and_remaining.first;
    ASSERT(first_healthy != -1 || first_degraded != -1);

    // Attempt to allocate the remainder to the first healthy priority first. If no such priority
    // exist, allocate to the first degraded priority.
    ASSERT(remaining_load < per_priority_load.healthy_priority_load_.get().size() +
                                per_priority_load.degraded_priority_load_.get().size());
    if (first_healthy != -1) {
      per_priority_load.healthy_priority_load_.get()[first_healthy] += remaining_load;
    } else {
      per_priority_load.degraded_priority_load_.get()[first_degraded] += remaining_load;
    }
  }

  // The allocated load between healthy and degraded should be exactly 100.
  ASSERT(100 == std::accumulate(per_priority_load.healthy_priority_load_.get().begin(),
                                per_priority_load.healthy_priority_load_.get().end(), 0) +
                    std::accumulate(per_priority_load.degraded_priority_load_.get().begin(),
                                    per_priority_load.degraded_priority_load_.get().end(), 0));
}

// Method iterates through priority levels and turns on/off panic mode.
void LoadBalancerBase::recalculatePerPriorityPanic() {
  per_priority_panic_.resize(priority_set_.hostSetsPerPriority().size());

  const uint32_t normalized_total_availability =
      calculateNormalizedTotalAvailability(per_priority_health_, per_priority_degraded_);

  const uint64_t panic_threshold = std::min<uint64_t>(
      100, runtime_.snapshot().getInteger(RuntimePanicThreshold, default_healthy_panic_percent_));

  // This is corner case when panic is disabled and there is no hosts available.
  // LoadBalancerBase::choosePriority method expects that the sum of
  // load percentages always adds up to 100.
  // To satisfy that requirement 100% is assigned to P=0.
  // In reality no traffic will be routed to P=0 priority, because
  // the panic mode is disabled and LoadBalancer will try to find
  // a healthy node and none is available.
  if (panic_threshold == 0 && normalized_total_availability == 0) {
    per_priority_load_.healthy_priority_load_.get()[0] = 100;
    return;
  }

  bool total_panic = true;
  for (size_t i = 0; i < per_priority_health_.get().size(); ++i) {
    // For each level check if it should run in panic mode. Never set panic mode if
    // normalized total health is 100%, even when individual priority level has very low # of
    // healthy hosts.
    const HostSet& priority_host_set = *priority_set_.hostSetsPerPriority()[i];
    per_priority_panic_[i] =
        (normalized_total_availability == 100 ? false : isHostSetInPanic(priority_host_set));
    total_panic = total_panic && per_priority_panic_[i];
  }

  // If all priority levels are in panic mode, load distribution
  // is done differently.
  if (total_panic) {
    recalculateLoadInTotalPanic();
  }
}

// recalculateLoadInTotalPanic method is called when all priority levels
// are in panic mode. The load distribution is done NOT based on number
// of healthy hosts in the priority, but based on number of hosts
// in each priority regardless of its health.
void LoadBalancerBase::recalculateLoadInTotalPanic() {
  // First calculate total number of hosts across all priorities regardless
  // whether they are healthy or not.
  const uint32_t total_hosts_count =
      std::accumulate(priority_set_.hostSetsPerPriority().begin(),
                      priority_set_.hostSetsPerPriority().end(), static_cast<size_t>(0),
                      [](size_t acc, const std::unique_ptr<Envoy::Upstream::HostSet>& host_set) {
                        return acc + host_set->hosts().size();
                      });

  if (0 == total_hosts_count) {
    // Backend is empty, but load must be distributed somewhere.
    per_priority_load_.healthy_priority_load_.get()[0] = 100;
    return;
  }

  // Now iterate through all priority levels and calculate how much
  // load is supposed to go to each priority. In panic mode the calculation
  // is based not on the number of healthy hosts but based on the number of
  // total hosts in the priority.
  uint32_t total_load = 100;
  int32_t first_noempty = -1;
  for (size_t i = 0; i < per_priority_panic_.size(); i++) {
    const HostSet& host_set = *priority_set_.hostSetsPerPriority()[i];
    const auto hosts_num = host_set.hosts().size();

    if ((-1 == first_noempty) && (0 != hosts_num)) {
      first_noempty = i;
    }
    const uint32_t priority_load = 100 * hosts_num / total_hosts_count;
    per_priority_load_.healthy_priority_load_.get()[i] = priority_load;
    per_priority_load_.degraded_priority_load_.get()[i] = 0;
    total_load -= priority_load;
  }

  // Add the remaining load to the first not empty load.
  per_priority_load_.healthy_priority_load_.get()[first_noempty] += total_load;

  // The total load should come up to 100%.
  ASSERT(100 == std::accumulate(per_priority_load_.healthy_priority_load_.get().begin(),
                                per_priority_load_.healthy_priority_load_.get().end(), 0));
}

std::pair<HostSet&, LoadBalancerBase::HostAvailability>
LoadBalancerBase::chooseHostSet(LoadBalancerContext* context) {
  if (context) {
    const auto priority_loads = context->determinePriorityLoad(priority_set_, per_priority_load_);

    const auto priority_and_source =
        choosePriority(random_.random(), priority_loads.healthy_priority_load_,
                       priority_loads.degraded_priority_load_);
    return {*priority_set_.hostSetsPerPriority()[priority_and_source.first],
            priority_and_source.second};
  }

  const auto priority_and_source =
      choosePriority(random_.random(), per_priority_load_.healthy_priority_load_,
                     per_priority_load_.degraded_priority_load_);
  return {*priority_set_.hostSetsPerPriority()[priority_and_source.first],
          priority_and_source.second};
}

ZoneAwareLoadBalancerBase::ZoneAwareLoadBalancerBase(
    const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterStats& stats,
    Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : LoadBalancerBase(priority_set, stats, runtime, random, common_config),
      local_priority_set_(local_priority_set),
      routing_enabled_(PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
          common_config.zone_aware_lb_config(), routing_enabled, 100, 100)),
      min_cluster_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(common_config.zone_aware_lb_config(),
                                                        min_cluster_size, 6U)),
      fail_traffic_on_panic_(common_config.zone_aware_lb_config().fail_traffic_on_panic()) {
  ASSERT(!priority_set.hostSetsPerPriority().empty());
  resizePerPriorityState();
  priority_set_.addPriorityUpdateCb(
      [this](uint32_t priority, const HostVector&, const HostVector&) -> void {
        // Make sure per_priority_state_ is as large as priority_set_.hostSetsPerPriority()
        resizePerPriorityState();
        // If P=0 changes, regenerate locality routing structures. Locality based routing is
        // disabled at all other levels.
        if (local_priority_set_ && priority == 0) {
          regenerateLocalityRoutingStructures();
        }
      });
  if (local_priority_set_) {
    // Multiple priorities are unsupported for local priority sets.
    // In order to support priorities correctly, one would have to make some assumptions about
    // routing (all local Envoys fail over at the same time) and use all priorities when computing
    // the locality routing structure.
    ASSERT(local_priority_set_->hostSetsPerPriority().size() == 1);
    local_priority_set_member_update_cb_handle_ = local_priority_set_->addPriorityUpdateCb(
        [this](uint32_t priority, const HostVector&, const HostVector&) -> void {
          ASSERT(priority == 0);
          // If the set of local Envoys changes, regenerate routing for P=0 as it does priority
          // based routing.
          regenerateLocalityRoutingStructures();
        });
  }
}

ZoneAwareLoadBalancerBase::~ZoneAwareLoadBalancerBase() {
  if (local_priority_set_member_update_cb_handle_ != nullptr) {
    local_priority_set_member_update_cb_handle_->remove();
  }
}

void ZoneAwareLoadBalancerBase::regenerateLocalityRoutingStructures() {
  ASSERT(local_priority_set_);
  stats_.lb_recalculate_zone_structures_.inc();
  // resizePerPriorityState should ensure these stay in sync.
  ASSERT(per_priority_state_.size() == priority_set_.hostSetsPerPriority().size());

  // We only do locality routing for P=0
  uint32_t priority = 0;
  PerPriorityState& state = *per_priority_state_[priority];
  // Do not perform any calculations if we cannot perform locality routing based on non runtime
  // params.
  if (earlyExitNonLocalityRouting()) {
    state.locality_routing_state_ = LocalityRoutingState::NoLocalityRouting;
    return;
  }
  HostSet& host_set = *priority_set_.hostSetsPerPriority()[priority];
  ASSERT(host_set.healthyHostsPerLocality().hasLocalLocality());
  const size_t num_localities = host_set.healthyHostsPerLocality().get().size();
  ASSERT(num_localities > 0);

  // It is worth noting that all of the percentages calculated are orthogonal from
  // how much load this priority level receives, percentageLoad(priority).
  //
  // If the host sets are such that 20% of load is handled locally and 80% is residual, and then
  // half the hosts in all host sets go unhealthy, this priority set will
  // still send half of the incoming load to the local locality and 80% to residual.
  //
  // Basically, fairness across localities within a priority is guaranteed. Fairness across
  // localities across priorities is not.
  absl::FixedArray<uint64_t> local_percentage(num_localities);
  calculateLocalityPercentage(localHostSet().healthyHostsPerLocality(), local_percentage.begin());
  absl::FixedArray<uint64_t> upstream_percentage(num_localities);
  calculateLocalityPercentage(host_set.healthyHostsPerLocality(), upstream_percentage.begin());

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
}

void ZoneAwareLoadBalancerBase::resizePerPriorityState() {
  const uint32_t size = priority_set_.hostSetsPerPriority().size();
  while (per_priority_state_.size() < size) {
    // Note for P!=0, PerPriorityState is created with NoLocalityRouting and never changed.
    per_priority_state_.push_back(std::make_unique<PerPriorityState>());
  }
}

bool ZoneAwareLoadBalancerBase::earlyExitNonLocalityRouting() {
  // We only do locality routing for P=0.
  HostSet& host_set = *priority_set_.hostSetsPerPriority()[0];
  if (host_set.healthyHostsPerLocality().get().size() < 2) {
    return true;
  }

  // lb_local_cluster_not_ok is bumped for "Local host set is not set or it is
  // panic mode for local cluster".
  if (!host_set.healthyHostsPerLocality().hasLocalLocality() ||
      host_set.healthyHostsPerLocality().get()[0].empty()) {
    stats_.lb_local_cluster_not_ok_.inc();
    return true;
  }

  // Same number of localities should be for local and upstream cluster.
  if (host_set.healthyHostsPerLocality().get().size() !=
      localHostSet().healthyHostsPerLocality().get().size()) {
    stats_.lb_zone_number_differs_.inc();
    return true;
  }

  // Do not perform locality routing for small clusters.
  const uint64_t min_cluster_size =
      runtime_.snapshot().getInteger(RuntimeMinClusterSize, min_cluster_size_);
  if (host_set.healthyHosts().size() < min_cluster_size) {
    stats_.lb_zone_cluster_too_small_.inc();
    return true;
  }

  return false;
}

HostConstSharedPtr LoadBalancerBase::chooseHost(LoadBalancerContext* context) {
  HostConstSharedPtr host;
  const size_t max_attempts = context ? context->hostSelectionRetryCount() + 1 : 1;
  for (size_t i = 0; i < max_attempts; ++i) {
    host = chooseHostOnce(context);

    // If host selection failed or the host is accepted by the filter, return.
    // Otherwise, try again.
    // Note: in the future we might want to allow retrying when chooseHostOnce returns nullptr.
    if (!host || !context || !context->shouldSelectAnotherHost(*host)) {
      return host;
    }
  }

  // If we didn't find anything, return the last host.
  return host;
}

bool LoadBalancerBase::isHostSetInPanic(const HostSet& host_set) {
  uint64_t global_panic_threshold = std::min<uint64_t>(
      100, runtime_.snapshot().getInteger(RuntimePanicThreshold, default_healthy_panic_percent_));
  const auto host_count = host_set.hosts().size() - host_set.excludedHosts().size();
  double healthy_percent =
      host_count == 0 ? 0.0 : 100.0 * host_set.healthyHosts().size() / host_count;

  double degraded_percent =
      host_count == 0 ? 0.0 : 100.0 * host_set.degradedHosts().size() / host_count;
  // If the % of healthy hosts in the cluster is less than our panic threshold, we use all hosts.
  if ((healthy_percent + degraded_percent) < global_panic_threshold) {
    return true;
  }

  return false;
}

void ZoneAwareLoadBalancerBase::calculateLocalityPercentage(
    const HostsPerLocality& hosts_per_locality, uint64_t* ret) {
  uint64_t total_hosts = 0;
  for (const auto& locality_hosts : hosts_per_locality.get()) {
    total_hosts += locality_hosts.size();
  }

  // TODO(snowp): Should we ignore excluded hosts here too?

  size_t i = 0;
  for (const auto& locality_hosts : hosts_per_locality.get()) {
    ret[i++] = total_hosts > 0 ? 10000ULL * locality_hosts.size() / total_hosts : 0;
  }
}

uint32_t ZoneAwareLoadBalancerBase::tryChooseLocalLocalityHosts(const HostSet& host_set) {
  PerPriorityState& state = *per_priority_state_[host_set.priority()];
  ASSERT(state.locality_routing_state_ != LocalityRoutingState::NoLocalityRouting);

  // At this point it's guaranteed to be at least 2 localities & local exists.
  const size_t number_of_localities = host_set.healthyHostsPerLocality().get().size();
  ASSERT(number_of_localities >= 2U);
  ASSERT(host_set.healthyHostsPerLocality().hasLocalLocality());

  // Try to push all of the requests to the same locality first.
  if (state.locality_routing_state_ == LocalityRoutingState::LocalityDirect) {
    stats_.lb_zone_routing_all_directly_.inc();
    return 0;
  }

  ASSERT(state.locality_routing_state_ == LocalityRoutingState::LocalityResidual);

  // If we cannot route all requests to the same locality, we already calculated how much we can
  // push to the local locality, check if we can push to local locality on current iteration.
  if (random_.random() % 10000 < state.local_percent_to_route_) {
    stats_.lb_zone_routing_sampled_.inc();
    return 0;
  }

  // At this point we must route cross locality as we cannot route to the local locality.
  stats_.lb_zone_routing_cross_zone_.inc();

  // This is *extremely* unlikely but possible due to rounding errors when calculating
  // locality percentages. In this case just select random locality.
  if (state.residual_capacity_[number_of_localities - 1] == 0) {
    stats_.lb_zone_no_capacity_left_.inc();
    return random_.random() % number_of_localities;
  }

  // Random sampling to select specific locality for cross locality traffic based on the additional
  // capacity in localities.
  uint64_t threshold = random_.random() % state.residual_capacity_[number_of_localities - 1];

  // This potentially can be optimized to be O(log(N)) where N is the number of localities.
  // Linear scan should be faster for smaller N, in most of the scenarios N will be small.
  // TODO(htuch): is there a bug here when threshold == 0? Seems like we pick
  // local locality in that situation. Probably should start iterating at 1.
  int i = 0;
  while (threshold > state.residual_capacity_[i]) {
    i++;
  }

  return i;
}

absl::optional<ZoneAwareLoadBalancerBase::HostsSource>
ZoneAwareLoadBalancerBase::hostSourceToUse(LoadBalancerContext* context) {
  auto host_set_and_source = chooseHostSet(context);

  // The second argument tells us which availability we should target from the selected host set.
  const auto host_availability = host_set_and_source.second;
  auto& host_set = host_set_and_source.first;
  HostsSource hosts_source;
  hosts_source.priority_ = host_set.priority();

  // If the selected host set has insufficient healthy hosts, return all hosts (unless we should
  // fail traffic on panic, in which case return no host).
  if (per_priority_panic_[hosts_source.priority_]) {
    stats_.lb_healthy_panic_.inc();
    if (fail_traffic_on_panic_) {
      return absl::nullopt;
    } else {
      hosts_source.source_type_ = HostsSource::SourceType::AllHosts;
      return hosts_source;
    }
  }

  // If we're doing locality weighted balancing, pick locality.
  absl::optional<uint32_t> locality;
  if (host_availability == HostAvailability::Degraded) {
    locality = host_set.chooseDegradedLocality();
  } else {
    locality = host_set.chooseHealthyLocality();
  }

  if (locality.has_value()) {
    hosts_source.source_type_ = localitySourceType(host_availability);
    hosts_source.locality_index_ = locality.value();
    return hosts_source;
  }

  // If we've latched that we can't do priority-based routing, return healthy or degraded hosts
  // for the selected host set.
  if (per_priority_state_[host_set.priority()]->locality_routing_state_ ==
      LocalityRoutingState::NoLocalityRouting) {
    hosts_source.source_type_ = sourceType(host_availability);
    return hosts_source;
  }

  // Determine if the load balancer should do zone based routing for this pick.
  if (!runtime_.snapshot().featureEnabled(RuntimeZoneEnabled, routing_enabled_)) {
    hosts_source.source_type_ = sourceType(host_availability);
    return hosts_source;
  }

  if (isHostSetInPanic(localHostSet())) {
    stats_.lb_local_cluster_not_ok_.inc();
    // If the local Envoy instances are in global panic, and we should not fail traffic, do
    // not do locality based routing.
    if (fail_traffic_on_panic_) {
      return absl::nullopt;
    } else {
      hosts_source.source_type_ = sourceType(host_availability);
      return hosts_source;
    }
  }

  hosts_source.source_type_ = localitySourceType(host_availability);
  hosts_source.locality_index_ = tryChooseLocalLocalityHosts(host_set);
  return hosts_source;
}

const HostVector& ZoneAwareLoadBalancerBase::hostSourceToHosts(HostsSource hosts_source) {
  const HostSet& host_set = *priority_set_.hostSetsPerPriority()[hosts_source.priority_];
  switch (hosts_source.source_type_) {
  case HostsSource::SourceType::AllHosts:
    return host_set.hosts();
  case HostsSource::SourceType::HealthyHosts:
    return host_set.healthyHosts();
  case HostsSource::SourceType::DegradedHosts:
    return host_set.degradedHosts();
  case HostsSource::SourceType::LocalityHealthyHosts:
    return host_set.healthyHostsPerLocality().get()[hosts_source.locality_index_];
  case HostsSource::SourceType::LocalityDegradedHosts:
    return host_set.degradedHostsPerLocality().get()[hosts_source.locality_index_];
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

EdfLoadBalancerBase::EdfLoadBalancerBase(
    const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterStats& stats,
    Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : ZoneAwareLoadBalancerBase(priority_set, local_priority_set, stats, runtime, random,
                                common_config),
      seed_(random_.random()) {
  // We fully recompute the schedulers for a given host set here on membership change, which is
  // consistent with what other LB implementations do (e.g. thread aware).
  // The downside of a full recompute is that time complexity is O(n * log n),
  // so we will need to do better at delta tracking to scale (see
  // https://github.com/envoyproxy/envoy/issues/2874).
  priority_set.addPriorityUpdateCb(
      [this](uint32_t priority, const HostVector&, const HostVector&) { refresh(priority); });
}

void EdfLoadBalancerBase::initialize() {
  for (uint32_t priority = 0; priority < priority_set_.hostSetsPerPriority().size(); ++priority) {
    refresh(priority);
  }
}

void EdfLoadBalancerBase::refresh(uint32_t priority) {
  const auto add_hosts_source = [this](HostsSource source, const HostVector& hosts) {
    // Nuke existing scheduler if it exists.
    auto& scheduler = scheduler_[source] = Scheduler{};
    refreshHostSource(source);

    // Check if the original host weights are equal and skip EDF creation if they are. When all
    // original weights are equal we can rely on unweighted host pick to do optimal round robin and
    // least-loaded host selection with lower memory and CPU overhead.
    if (hostWeightsAreEqual(hosts)) {
      // Skip edf creation.
      return;
    }

    scheduler.edf_ = std::make_unique<EdfScheduler<const Host>>();

    // Populate scheduler with host list.
    // TODO(mattklein123): We must build the EDF schedule even if all of the hosts are currently
    // weighted 1. This is because currently we don't refresh host sets if only weights change.
    // We should probably change this to refresh at all times. See the comment in
    // BaseDynamicClusterImpl::updateDynamicHostList about this.
    for (const auto& host : hosts) {
      // We use a fixed weight here. While the weight may change without
      // notification, this will only be stale until this host is next picked,
      // at which point it is reinserted into the EdfScheduler with its new
      // weight in chooseHost().
      scheduler.edf_->add(hostWeight(*host), host);
    }

    // Cycle through hosts to achieve the intended offset behavior.
    // TODO(htuch): Consider how we can avoid biasing towards earlier hosts in the schedule across
    // refreshes for the weighted case.
    if (!hosts.empty()) {
      for (uint32_t i = 0; i < seed_ % hosts.size(); ++i) {
        auto host = scheduler.edf_->pick();
        scheduler.edf_->add(hostWeight(*host), host);
      }
    }
  };

  // Populate EdfSchedulers for each valid HostsSource value for the host set at this priority.
  const auto& host_set = priority_set_.hostSetsPerPriority()[priority];
  add_hosts_source(HostsSource(priority, HostsSource::SourceType::AllHosts), host_set->hosts());
  add_hosts_source(HostsSource(priority, HostsSource::SourceType::HealthyHosts),
                   host_set->healthyHosts());
  add_hosts_source(HostsSource(priority, HostsSource::SourceType::DegradedHosts),
                   host_set->degradedHosts());
  for (uint32_t locality_index = 0;
       locality_index < host_set->healthyHostsPerLocality().get().size(); ++locality_index) {
    add_hosts_source(
        HostsSource(priority, HostsSource::SourceType::LocalityHealthyHosts, locality_index),
        host_set->healthyHostsPerLocality().get()[locality_index]);
  }
  for (uint32_t locality_index = 0;
       locality_index < host_set->degradedHostsPerLocality().get().size(); ++locality_index) {
    add_hosts_source(
        HostsSource(priority, HostsSource::SourceType::LocalityDegradedHosts, locality_index),
        host_set->degradedHostsPerLocality().get()[locality_index]);
  }
}

HostConstSharedPtr EdfLoadBalancerBase::chooseHostOnce(LoadBalancerContext* context) {
  const absl::optional<HostsSource> hosts_source = hostSourceToUse(context);
  if (!hosts_source) {
    return nullptr;
  }
  auto scheduler_it = scheduler_.find(*hosts_source);
  // We should always have a scheduler for any return value from
  // hostSourceToUse() via the construction in refresh();
  ASSERT(scheduler_it != scheduler_.end());
  auto& scheduler = scheduler_it->second;

  // As has been commented in both EdfLoadBalancerBase::refresh and
  // BaseDynamicClusterImpl::updateDynamicHostList, we must do a runtime pivot here to determine
  // whether to use EDF or do unweighted (fast) selection. EDF is non-null iff the original weights
  // of 2 or more hosts differ.
  if (scheduler.edf_ != nullptr) {
    auto host = scheduler.edf_->pick();
    if (host != nullptr) {
      scheduler.edf_->add(hostWeight(*host), host);
    }
    return host;
  } else {
    const HostVector& hosts_to_use = hostSourceToHosts(*hosts_source);
    if (hosts_to_use.empty()) {
      return nullptr;
    }
    return unweightedHostPick(hosts_to_use, *hosts_source);
  }
}

HostConstSharedPtr LeastRequestLoadBalancer::unweightedHostPick(const HostVector& hosts_to_use,
                                                                const HostsSource&) {
  HostSharedPtr candidate_host = nullptr;
  for (uint32_t choice_idx = 0; choice_idx < choice_count_; ++choice_idx) {
    const int rand_idx = random_.random() % hosts_to_use.size();
    HostSharedPtr sampled_host = hosts_to_use[rand_idx];

    if (candidate_host == nullptr) {
      // Make a first choice to start the comparisons.
      candidate_host = sampled_host;
      continue;
    }

    const auto candidate_active_rq = candidate_host->stats().rq_active_.value();
    const auto sampled_active_rq = sampled_host->stats().rq_active_.value();
    if (sampled_active_rq < candidate_active_rq) {
      candidate_host = sampled_host;
    }
  }

  return candidate_host;
}

HostConstSharedPtr RandomLoadBalancer::chooseHostOnce(LoadBalancerContext* context) {
  const absl::optional<HostsSource> hosts_source = hostSourceToUse(context);
  if (!hosts_source) {
    return nullptr;
  }

  const HostVector& hosts_to_use = hostSourceToHosts(*hosts_source);
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  return hosts_to_use[random_.random() % hosts_to_use.size()];
}

SubsetSelectorImpl::SubsetSelectorImpl(
    const Protobuf::RepeatedPtrField<std::string>& selector_keys,
    envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
        LbSubsetSelectorFallbackPolicy fallback_policy,
    const Protobuf::RepeatedPtrField<std::string>& fallback_keys_subset)
    : selector_keys_(selector_keys.begin(), selector_keys.end()), fallback_policy_(fallback_policy),
      fallback_keys_subset_(fallback_keys_subset.begin(), fallback_keys_subset.end()) {

  if (fallback_policy_ !=
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET) {
    // defining fallback_keys_subset_ for a fallback policy other than KEYS_SUBSET doesn't have
    // any effect and it is probably a user mistake. We should let the user know about it.
    if (!fallback_keys_subset_.empty()) {
      throw EnvoyException("fallback_keys_subset can be set only for KEYS_SUBSET fallback_policy");
    }
    return;
  }

  // if KEYS_SUBSET fallback policy is selected, fallback_keys_subset must not be empty, because
  // it would be the same as not defining fallback policy at all (global fallback policy would be
  // used)
  if (fallback_keys_subset_.empty()) {
    throw EnvoyException("fallback_keys_subset cannot be empty");
  }

  // We allow only for a fallback to a subset of the selector keys because this is probably the
  // only use case that makes sense (fallback from more specific selector to less specific
  // selector). Potentially we can relax this constraint in the future if there will be a use case
  // for this.
  if (!std::includes(selector_keys_.begin(), selector_keys_.end(), fallback_keys_subset_.begin(),
                     fallback_keys_subset_.end())) {
    throw EnvoyException("fallback_keys_subset must be a subset of selector keys");
  }

  // Enforce that the fallback_keys_subset_ set is smaller than the selector_keys_ set. Otherwise
  // we could end up with a infinite recursion of SubsetLoadBalancer::chooseHost().
  if (selector_keys_.size() == fallback_keys_subset_.size()) {
    throw EnvoyException("fallback_keys_subset cannot be equal to keys");
  }
}
} // namespace Upstream
} // namespace Envoy
