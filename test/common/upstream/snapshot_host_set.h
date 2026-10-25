#pragma once

#include <memory>

#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

// A host set that keeps flat vectors but publishes them as a snapshot, and adopts a snapshot it is
// given. It exercises the host set hooks without a persistent representation.
class SnapshotHostSet : public HostSetImpl {
public:
  using HostSetImpl::HostSetImpl;

  static HostSetFactory factory() {
    return [](uint32_t priority, std::optional<bool> weighted_priority_health,
              std::optional<uint32_t> overprovisioning_factor) -> HostSetImplPtr {
      return std::make_unique<SnapshotHostSet>(priority, weighted_priority_health,
                                               overprovisioning_factor);
    };
  }

  // Wraps `params` in a snapshot that SnapshotHostSet adopts.
  static PersistentHostPartitionsSnapshotConstSharedPtr
  makeSnapshot(PrioritySet::UpdateHostsParams params) {
    return std::make_shared<const Snapshot>(std::move(params));
  }

  // HostSet
  PersistentHostPartitionsSnapshotConstSharedPtr persistentPartitions() const override {
    return snapshot_;
  }

  // HostSetImpl
  void updateHosts(PrioritySet::UpdateHostsParams&& update_hosts_params,
                   LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
                   const HostVector& hosts_removed,
                   std::optional<bool> weighted_priority_health = std::nullopt,
                   std::optional<uint32_t> overprovisioning_factor = std::nullopt) override {
    if (update_hosts_params.persistent_partitions != nullptr) {
      snapshot_ = std::static_pointer_cast<const Snapshot>(
          std::move(update_hosts_params.persistent_partitions));
      update_hosts_params = snapshot_->flatUpdateHostsParams();
    } else {
      snapshot_ = std::make_shared<const Snapshot>(update_hosts_params);
    }
    HostSetImpl::updateHosts(std::move(update_hosts_params), std::move(locality_weights),
                             hosts_added, hosts_removed, weighted_priority_health,
                             overprovisioning_factor);
  }

private:
  class Snapshot : public HostSetSnapshotBase {
  public:
    explicit Snapshot(PrioritySet::UpdateHostsParams params) : params_(std::move(params)) {}

    HostSetFactory hostSetFactory() const override { return factory(); }
    PrioritySet::UpdateHostsParams flatUpdateHostsParams() const override { return params_; }

  private:
    const PrioritySet::UpdateHostsParams params_;
  };

  std::shared_ptr<const Snapshot> snapshot_;
};

} // namespace Upstream
} // namespace Envoy
