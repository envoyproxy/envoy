#include "source/extensions/load_balancing_policies/common/locality_wrr.h"

namespace Envoy {
namespace Upstream {

LocalityWrr::LocalityWrr(const HostSet& host_set, uint64_t seed) {
  rebuildLocalityScheduler(healthy_locality_scheduler_, healthy_locality_entries_,
                           host_set.healthyHostsPerLocality(), host_set.healthyHosts(),
                           host_set.hostsPerLocalityPtr(), host_set.excludedHostsPerLocalityPtr(),
                           host_set.localityWeights(), host_set.overprovisioningFactor(), seed);
  rebuildLocalityScheduler(degraded_locality_scheduler_, degraded_locality_entries_,
                           host_set.degradedHostsPerLocality(), host_set.degradedHosts(),
                           host_set.hostsPerLocalityPtr(), host_set.excludedHostsPerLocalityPtr(),
                           host_set.localityWeights(), host_set.overprovisioningFactor(), seed);
}

absl::optional<uint32_t> LocalityWrr::chooseHealthyLocality() {
  return chooseLocality(healthy_locality_scheduler_.get());
}

absl::optional<uint32_t> LocalityWrr::chooseDegradedLocality() {
  return chooseLocality(degraded_locality_scheduler_.get());
}

void LocalityWrr::rebuildLocalityScheduler(
    std::unique_ptr<EdfScheduler<LocalityEntry>>& locality_scheduler,
    std::vector<std::shared_ptr<LocalityEntry>>& locality_entries,
    const HostsPerLocality& eligible_hosts_per_locality, const HostVector& eligible_hosts,
    HostsPerLocalityConstSharedPtr all_hosts_per_locality,
    HostsPerLocalityConstSharedPtr excluded_hosts_per_locality,
    LocalityWeightsConstSharedPtr locality_weights, uint32_t overprovisioning_factor,
    uint64_t seed) {
  // Rebuild the locality scheduler by computing the effective weight of each
  // locality in this priority. The scheduler is reset by default, and is rebuilt only if we have
  // locality weights (i.e. using EDS) and there is at least one eligible host in this priority.
  //
  // We omit building a scheduler when there are zero eligible hosts in the priority as
  // all the localities will have zero effective weight. At selection time, we'll either select
  // from a different scheduler or there will be no available hosts in the priority. At that point
  // we'll rely on other mechanisms such as panic mode to select a host, none of which rely on the
  // scheduler.
  //
  // TODO(htuch): if the underlying locality index ->
  // envoy::config::core::v3::Locality hasn't changed in hosts_/healthy_hosts_/degraded_hosts_, we
  // could just update locality_weight_ without rebuilding. Similar to how host
  // level WRR works, we would age out the existing entries via picks and lazily
  // apply the new weights.
  locality_scheduler = nullptr;
  if (all_hosts_per_locality != nullptr && locality_weights != nullptr &&
      !locality_weights->empty() && !eligible_hosts.empty()) {
    locality_entries.clear();
    for (uint32_t i = 0; i < all_hosts_per_locality->get().size(); ++i) {
      const double effective_weight = effectiveLocalityWeight(
          i, eligible_hosts_per_locality, *excluded_hosts_per_locality, *all_hosts_per_locality,
          *locality_weights, overprovisioning_factor);
      if (effective_weight > 0) {
        locality_entries.emplace_back(std::make_shared<LocalityEntry>(i, effective_weight));
      }
    }
    // If not all effective weights were zero, create the scheduler.
    if (!locality_entries.empty()) {
      locality_scheduler = std::make_unique<EdfScheduler<LocalityEntry>>(
          EdfScheduler<LocalityEntry>::createWithPicks(
              locality_entries, [](const LocalityEntry& entry) { return entry.effective_weight_; },
              seed));
    }
  }
}

absl::optional<uint32_t>
LocalityWrr::chooseLocality(EdfScheduler<LocalityEntry>* locality_scheduler) {
  if (locality_scheduler == nullptr) {
    return {};
  }
  const std::shared_ptr<LocalityEntry> locality = locality_scheduler->pickAndAdd(
      [](const LocalityEntry& locality) { return locality.effective_weight_; });
  // We don't build a schedule if there are no weighted localities, so we should always succeed.
  ASSERT(locality != nullptr);
  // If we picked it before, its weight must have been positive.
  ASSERT(locality->effective_weight_ > 0);
  return locality->index_;
}

double LocalityWrr::effectiveLocalityWeight(uint32_t index,
                                            const HostsPerLocality& eligible_hosts_per_locality,
                                            const HostsPerLocality& excluded_hosts_per_locality,
                                            const HostsPerLocality& all_hosts_per_locality,
                                            const LocalityWeights& locality_weights,
                                            uint32_t overprovisioning_factor) {
  const auto& locality_eligible_hosts = eligible_hosts_per_locality.get()[index];
  const uint32_t excluded_count = excluded_hosts_per_locality.get().size() > index
                                      ? excluded_hosts_per_locality.get()[index].size()
                                      : 0;
  const auto host_count = all_hosts_per_locality.get()[index].size() - excluded_count;
  if (host_count == 0) {
    return 0.0;
  }
  const double locality_availability_ratio = 1.0 * locality_eligible_hosts.size() / host_count;
  const uint32_t weight = locality_weights[index];
  // Availability ranges from 0-1.0, and is the ratio of eligible hosts to total hosts, modified
  // by the overprovisioning factor.
  const double effective_locality_availability_ratio =
      std::min(1.0, (overprovisioning_factor / 100.0) * locality_availability_ratio);
  return weight * effective_locality_availability_ratio;
}

} // namespace Upstream
} // namespace Envoy
