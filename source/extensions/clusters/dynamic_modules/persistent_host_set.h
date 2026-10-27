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

  // An Upstream::HostSetFactory.
  static Upstream::HostSetImplPtr create(uint32_t priority,
                                         std::optional<bool> weighted_priority_health,
                                         std::optional<uint32_t> overprovisioning_factor) {
    return std::make_unique<PersistentHostSet>(priority, weighted_priority_health,
                                               overprovisioning_factor);
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
  Upstream::HostCounts hostCounts() const override;

  Upstream::HostSetSnapshotConstSharedPtr snapshot() const override;

  // Upstream::HostSetImpl
  // Accepts flat views, a snapshot from another PersistentHostSet (the main-thread copy of this
  // set, when this one runs on a worker), or the snapshot this set returned from a change below.
  void updateHosts(Upstream::PrioritySet::UpdateHostsParams&& update_hosts_params,
                   Upstream::LocalityWeightsConstSharedPtr locality_weights,
                   const Upstream::HostVector& hosts_added,
                   const Upstream::HostVector& hosts_removed,
                   std::optional<bool> weighted_priority_health = std::nullopt,
                   std::optional<uint32_t> overprovisioning_factor = std::nullopt) override;

  // Patches the partitions with a membership delta and returns their snapshot, to be passed to
  // PrioritySet::updateHosts() with the same delta. Locality weights are not supported.
  Upstream::HostSetSnapshotConstSharedPtr
  applyMembershipDelta(const Upstream::HostVector& hosts_added,
                       const Upstream::HostVector& hosts_removed);
  // Re-partitions `host` after its health changed and returns the snapshot, or nullptr if this set
  // does not hold `host`.
  Upstream::HostSetSnapshotConstSharedPtr
  applyHostHealthChange(const Upstream::HostSharedPtr& host);
  // Re-partitions every host after an unknown set of health changes and returns the snapshot.
  Upstream::HostSetSnapshotConstSharedPtr refreshHealth();

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
  Upstream::HostSetSnapshotConstSharedPtr currentSnapshot() const;
  // Makes `partitions_` current so a delta can be applied to it.
  void syncPartitions();
  // Marks `partitions_` current, drops the stale flat views, and returns their snapshot.
  Upstream::HostSetSnapshotConstSharedPtr
  partitionsChanged(std::optional<Upstream::HostVector> health_changed);

  std::unique_ptr<Partitions> partitions_;
  // Adopted from another host set; current while `source_` is Snapshot.
  Upstream::HostSetSnapshotConstSharedPtr adopted_snapshot_;
  // Snapshot of `partitions_`, built on demand while `source_` is Partitions.
  mutable Upstream::HostSetSnapshotConstSharedPtr partitions_snapshot_;
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
