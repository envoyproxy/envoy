#pragma once

#include <memory>
#include <optional>

#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

/**
 * Host set whose partitions are persistent maps patched by membership and health deltas. Flat views
 * are built on the first read after a change. A full `updateHosts()` replaces the partitions.
 */
class PersistentHostSet : public Upstream::HostSetImpl {
public:
  PersistentHostSet(uint32_t priority, std::optional<bool> weighted_priority_health,
                    std::optional<uint32_t> overprovisioning_factor);
  ~PersistentHostSet() override;

  static Upstream::HostSetFactory factory() {
    return [](uint32_t priority, std::optional<bool> weighted_priority_health,
              std::optional<uint32_t> overprovisioning_factor) -> Upstream::HostSetImplPtr {
      return std::make_unique<PersistentHostSet>(priority, weighted_priority_health,
                                                 overprovisioning_factor);
    };
  }

  // Upstream::HostSet
  const Upstream::HostVector& hosts() const override { return *flat().hosts; }
  Upstream::HostVectorConstSharedPtr hostsPtr() const override { return flat().hosts; }
  const Upstream::HostVector& healthyHosts() const override { return flat().healthy_hosts->get(); }
  Upstream::HealthyHostVectorConstSharedPtr healthyHostsPtr() const override {
    return flat().healthy_hosts;
  }
  const Upstream::HostVector& degradedHosts() const override {
    return flat().degraded_hosts->get();
  }
  Upstream::DegradedHostVectorConstSharedPtr degradedHostsPtr() const override {
    return flat().degraded_hosts;
  }
  const Upstream::HostVector& excludedHosts() const override {
    return flat().excluded_hosts->get();
  }
  Upstream::ExcludedHostVectorConstSharedPtr excludedHostsPtr() const override {
    return flat().excluded_hosts;
  }
  const Upstream::HostsPerLocality& hostsPerLocality() const override {
    return *flat().hosts_per_locality;
  }
  Upstream::HostsPerLocalityConstSharedPtr hostsPerLocalityPtr() const override {
    return flat().hosts_per_locality;
  }
  const Upstream::HostsPerLocality& healthyHostsPerLocality() const override {
    return *flat().healthy_hosts_per_locality;
  }
  Upstream::HostsPerLocalityConstSharedPtr healthyHostsPerLocalityPtr() const override {
    return flat().healthy_hosts_per_locality;
  }
  const Upstream::HostsPerLocality& degradedHostsPerLocality() const override {
    return *flat().degraded_hosts_per_locality;
  }
  Upstream::HostsPerLocalityConstSharedPtr degradedHostsPerLocalityPtr() const override {
    return flat().degraded_hosts_per_locality;
  }
  const Upstream::HostsPerLocality& excludedHostsPerLocality() const override {
    return *flat().excluded_hosts_per_locality;
  }
  Upstream::HostsPerLocalityConstSharedPtr excludedHostsPerLocalityPtr() const override {
    return flat().excluded_hosts_per_locality;
  }
  size_t hostCount() const override;
  size_t healthyHostCount() const override;
  size_t degradedHostCount() const override;
  size_t excludedHostCount() const override;

  Upstream::PersistentHostPartitionsSnapshotConstSharedPtr persistentPartitions() const override;

  // Upstream::HostSetImpl
  // Accepts either flat views or a snapshot from another PersistentHostSet (the main-thread copy of
  // this set, when this one runs on a worker).
  void updateHosts(Upstream::PrioritySet::UpdateHostsParams&& update_hosts_params,
                   Upstream::LocalityWeightsConstSharedPtr locality_weights,
                   const Upstream::HostVector& hosts_added,
                   const Upstream::HostVector& hosts_removed,
                   std::optional<bool> weighted_priority_health = std::nullopt,
                   std::optional<uint32_t> overprovisioning_factor = std::nullopt) override;
  // Places each added host by its health and locality. Locality weights are not supported.
  bool applyMembershipDelta(const Upstream::HostVector& hosts_added,
                            const Upstream::HostVector& hosts_removed,
                            std::optional<bool> weighted_priority_health,
                            std::optional<uint32_t> overprovisioning_factor) override;
  bool applyHostHealthChange(const Upstream::HostSharedPtr& host) override;

  // Hosts whose health changed in the update that produced the current partitions, or nullopt when
  // that update replaced the partitions wholesale.
  const std::optional<Upstream::HostVector>& healthChangedHosts() const { return health_changed_; }

private:
  struct Partitions;
  // Which representation is current.
  enum class Source { Flat, Partitions, Snapshot };

  // Returns the flat views, building them from the current partitions or snapshot if needed.
  const Upstream::PrioritySet::UpdateHostsParams& flat() const;
  // The snapshot of the current partitions, or nullptr when the flat views are current.
  Upstream::PersistentHostPartitionsSnapshotConstSharedPtr currentSnapshot() const;
  // Makes `partitions_` current so a delta can be applied to it.
  void syncPartitions();
  // Marks `partitions_` current, drops the stale flat views, and runs the update callbacks.
  void publishPartitionsChange(const Upstream::HostVector& hosts_added,
                               const Upstream::HostVector& hosts_removed,
                               Upstream::HostVector health_changed,
                               std::optional<bool> weighted_priority_health,
                               std::optional<uint32_t> overprovisioning_factor);

  std::unique_ptr<Partitions> partitions_;
  // Adopted from another host set; current while `source_` is Snapshot.
  Upstream::PersistentHostPartitionsSnapshotConstSharedPtr adopted_snapshot_;
  // Snapshot of `partitions_`, built on demand while `source_` is Partitions.
  mutable Upstream::PersistentHostPartitionsSnapshotConstSharedPtr partitions_snapshot_;
  // Flat views: the source after a full update, otherwise a cache built on first read.
  mutable Upstream::PrioritySet::UpdateHostsParams flat_;
  mutable bool flat_built_{true};
  std::optional<Upstream::HostVector> health_changed_;
  Source source_{Source::Flat};
};

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
