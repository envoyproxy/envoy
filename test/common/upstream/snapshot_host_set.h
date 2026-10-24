#pragma once

#include <algorithm>
#include <memory>

#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

// A host set that applies deltas and health changes by re-partitioning its flat vectors and hands
// workers a snapshot. It exercises the host set hooks without a persistent representation.
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
      snapshot_ = nullptr;
    }
    HostSetImpl::updateHosts(std::move(update_hosts_params), std::move(locality_weights),
                             hosts_added, hosts_removed, weighted_priority_health,
                             overprovisioning_factor);
  }

  bool applyMembershipDelta(const HostVector& hosts_added, const HostVector& hosts_removed,
                            std::optional<bool> weighted_priority_health,
                            std::optional<uint32_t> overprovisioning_factor) override {
    auto hosts = std::make_shared<HostVector>();
    for (const auto& host : this->hosts()) {
      if (std::find(hosts_removed.begin(), hosts_removed.end(), host) == hosts_removed.end()) {
        hosts->push_back(host);
      }
    }
    hosts->insert(hosts->end(), hosts_added.begin(), hosts_added.end());
    publish(partitionHosts(std::move(hosts), HostsPerLocalityImpl::empty()), hosts_added,
            hosts_removed, weighted_priority_health, overprovisioning_factor);
    return true;
  }

  bool applyHostHealthChange(const HostSharedPtr& host) override {
    if (std::find(hosts().begin(), hosts().end(), host) == hosts().end()) {
      return false;
    }
    publish(partitionHosts(hostsPtr(), hostsPerLocalityPtr()), {}, {}, std::nullopt, std::nullopt);
    return true;
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

  // The snapshot is set before the update callbacks run, since they hand it to workers.
  void publish(PrioritySet::UpdateHostsParams&& params, const HostVector& hosts_added,
               const HostVector& hosts_removed, std::optional<bool> weighted_priority_health,
               std::optional<uint32_t> overprovisioning_factor) {
    snapshot_ = std::make_shared<const Snapshot>(params);
    HostSetImpl::updateHosts(std::move(params), localityWeights(), hosts_added, hosts_removed,
                             weighted_priority_health, overprovisioning_factor);
  }

  std::shared_ptr<const Snapshot> snapshot_;
};

} // namespace Upstream
} // namespace Envoy
