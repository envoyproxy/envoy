#pragma once

#include <memory>
#include <optional>

#include "source/common/upstream/upstream_impl.h"

#include "absl/functional/function_ref.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

/**
 * Host set whose partitions are persistent maps patched by membership and health deltas. Each flat
 * view is built on its first read after a change.
 *
 * The main thread's set starts empty and only changes through applyMembershipDelta(),
 * applyHostHealthChange() and refreshHealth(), each followed by updateHosts() with the snapshot it
 * returned. A worker's set only adopts snapshots from the main thread. Neither receives a non-empty
 * flat update.
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
  const Upstream::HostVector& hosts() const override { return *hostsPtr(); }
  Upstream::HostVectorConstSharedPtr hostsPtr() const override;
  const Upstream::HostVector& healthyHosts() const override { return healthyHostsPtr()->get(); }
  Upstream::HealthyHostVectorConstSharedPtr healthyHostsPtr() const override;
  const Upstream::HostVector& degradedHosts() const override { return degradedHostsPtr()->get(); }
  Upstream::DegradedHostVectorConstSharedPtr degradedHostsPtr() const override;
  const Upstream::HostVector& excludedHosts() const override { return excludedHostsPtr()->get(); }
  Upstream::ExcludedHostVectorConstSharedPtr excludedHostsPtr() const override;
  const Upstream::HostsPerLocality& hostsPerLocality() const override {
    return *hostsPerLocalityPtr();
  }
  Upstream::HostsPerLocalityConstSharedPtr hostsPerLocalityPtr() const override;
  const Upstream::HostsPerLocality& healthyHostsPerLocality() const override {
    return *healthyHostsPerLocalityPtr();
  }
  Upstream::HostsPerLocalityConstSharedPtr healthyHostsPerLocalityPtr() const override;
  const Upstream::HostsPerLocality& degradedHostsPerLocality() const override {
    return *degradedHostsPerLocalityPtr();
  }
  Upstream::HostsPerLocalityConstSharedPtr degradedHostsPerLocalityPtr() const override;
  const Upstream::HostsPerLocality& excludedHostsPerLocality() const override {
    return *excludedHostsPerLocalityPtr();
  }
  Upstream::HostsPerLocalityConstSharedPtr excludedHostsPerLocalityPtr() const override;
  Upstream::HostCounts hostCounts() const override;

  // Hands out the current snapshot. Health changes after this start a new health-changed list.
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

  // The changes below each return a snapshot that must reach updateHosts() before the next change.
  // Patches the partitions with a membership delta. A host in both lists ends up present. Locality
  // weights are not supported.
  Upstream::HostSetSnapshotConstSharedPtr
  applyMembershipDelta(const Upstream::HostVector& hosts_added,
                       const Upstream::HostVector& hosts_removed);
  // Re-partitions `host` after its health changed, or returns nullptr if this set does not hold it.
  Upstream::HostSetSnapshotConstSharedPtr
  applyHostHealthChange(const Upstream::HostSharedPtr& host);
  // Re-partitions every host after an unknown set of health changes.
  Upstream::HostSetSnapshotConstSharedPtr refreshHealth();

  // Members whose health changed since the previous snapshot() hand-out, in the current snapshot,
  // or nullopt when that is unknown. Hosts removed since then are not listed.
  std::optional<Upstream::HostVector> healthChangedHosts() const;

  // Calls `fn` for each healthy host, in healthyHosts() order, without building the flat view.
  void forEachHealthyHost(absl::FunctionRef<void(const Upstream::HostSharedPtr&)> fn) const;

private:
  struct Partitions;
  // Which representation is current.
  enum class Source { Flat, Partitions, Snapshot };

  // The snapshot of the current partitions, or nullptr when the flat views are current.
  Upstream::HostSetSnapshotConstSharedPtr currentSnapshot() const;
  // Makes `partitions_` current and starts a new health-changed list if the last one was handed
  // out, so a change can be applied.
  void beginChange();
  // Marks `partitions_` current, drops the stale flat views, and returns their snapshot.
  Upstream::HostSetSnapshotConstSharedPtr partitionsChanged();

  std::unique_ptr<Partitions> partitions_;
  // Adopted from another host set; current while `source_` is Snapshot.
  Upstream::HostSetSnapshotConstSharedPtr adopted_snapshot_;
  // Snapshot of `partitions_`, built on demand while `source_` is Partitions.
  mutable Upstream::HostSetSnapshotConstSharedPtr partitions_snapshot_;
  // Flat views: the source after a full update, otherwise each one is built on its first read.
  mutable Upstream::PrioritySet::UpdateHostsParams flat_;
  // Whether snapshot() handed out the current health-changed list.
  mutable bool health_changes_handed_out_{false};
  // Whether the snapshot of the last change has yet to reach updateHosts().
  bool change_pending_{false};
  Source source_{Source::Flat};
};

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
