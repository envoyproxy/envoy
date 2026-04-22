#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/upstream/upstream.h"

#include "source/common/upstream/edf_scheduler.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {

class LocalityWrr {
public:
  explicit LocalityWrr(const HostSet& host_set, uint64_t seed);

  absl::optional<uint32_t> chooseHealthyLocality();
  absl::optional<uint32_t> chooseDegradedLocality();

private:
  struct LocalityEntry {
    LocalityEntry(uint32_t index, double effective_weight)
        : index_(index), effective_weight_(effective_weight) {}
    const uint32_t index_;
    const double effective_weight_;
  };

  // Rebuilds the provided locality scheduler with locality entries based on the locality weights
  // and eligible hosts.
  //
  // @param locality_scheduler the locality scheduler to rebuild. Will be set to nullptr if no
  // localities are eligible.
  // @param locality_entries the vector that holds locality entries. Will be reset and populated
  // with entries corresponding to the new scheduler.
  // @param eligible_hosts_per_locality eligible hosts for this scheduler grouped by locality.
  // @param eligible_hosts all eligible hosts for this scheduler.
  // @param all_hosts_per_locality all hosts for this HostSet grouped by locality.
  // @param locality_weights the weighting of each locality.
  // @param overprovisioning_factor the overprovisioning factor to use when computing the effective
  // weight of a locality.
  // @param seed a random number of initial picks to "invoke" on the locality scheduler. This
  // allows to distribute the load between different localities across worker threads and a fleet
  // of Envoys.
  static void
  rebuildLocalityScheduler(std::unique_ptr<EdfScheduler<LocalityEntry>>& locality_scheduler,
                           std::vector<std::shared_ptr<LocalityEntry>>& locality_entries,
                           const HostsPerLocality& eligible_hosts_per_locality,
                           const HostVector& eligible_hosts,
                           HostsPerLocalityConstSharedPtr all_hosts_per_locality,
                           HostsPerLocalityConstSharedPtr excluded_hosts_per_locality,
                           LocalityWeightsConstSharedPtr locality_weights,
                           uint32_t overprovisioning_factor, uint64_t seed);
  // Weight for a locality taking into account health status using the provided eligible hosts per
  // locality.
  static double effectiveLocalityWeight(uint32_t index,
                                        const HostsPerLocality& eligible_hosts_per_locality,
                                        const HostsPerLocality& excluded_hosts_per_locality,
                                        const HostsPerLocality& all_hosts_per_locality,
                                        const LocalityWeights& locality_weights,
                                        uint32_t overprovisioning_factor);

  static absl::optional<uint32_t> chooseLocality(EdfScheduler<LocalityEntry>* locality_scheduler);

  std::vector<std::shared_ptr<LocalityEntry>> healthy_locality_entries_;
  std::unique_ptr<EdfScheduler<LocalityEntry>> healthy_locality_scheduler_;
  std::vector<std::shared_ptr<LocalityEntry>> degraded_locality_entries_;
  std::unique_ptr<EdfScheduler<LocalityEntry>> degraded_locality_scheduler_;
};

} // namespace Upstream
} // namespace Envoy
