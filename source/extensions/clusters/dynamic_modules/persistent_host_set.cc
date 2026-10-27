#include "source/extensions/clusters/dynamic_modules/persistent_host_set.h"

#include <memory>
#include <vector>

#include "envoy/upstream/locality.h"

#include "source/common/common/persistent_hash_map.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

using Upstream::DegradedHostVector;
using Upstream::ExcludedHostVector;
using Upstream::HealthyHostVector;
using Upstream::Host;
using Upstream::HostCounts;
using Upstream::HostSetImpl;
using Upstream::HostSetSnapshot;
using Upstream::HostSetSnapshotConstSharedPtr;
using Upstream::HostSharedPtr;
using Upstream::HostsPerLocality;
using Upstream::HostsPerLocalityImpl;
using Upstream::HostVector;
using Upstream::LocalityWeightsConstSharedPtr;
using Upstream::PrioritySet;

namespace {

// A persistent set of hosts keyed by host. Copying it shares its nodes.
using HostsByHost = PersistentHashMap<const Host*, HostSharedPtr>;

// The all, healthy, degraded and excluded views of one host population, as persistent maps.
struct PersistentViews {
  HostsByHost all;
  HostsByHost healthy;
  HostsByHost degraded;
  HostsByHost excluded;
};

// The snapshot PersistentHostSet hands to workers. Copying a HostsByHost shares its nodes, so a
// snapshot costs one copy per view and locality, not per host.
class PartitionsSnapshotImpl : public HostSetSnapshot {
public:
  PrioritySet::UpdateHostsParams flatUpdateHostsParams() const;

  PersistentViews hosts;
  std::vector<PersistentViews> localities;
  bool has_local_locality{false};
  // See PersistentHostSet::healthChangedHosts().
  std::optional<HostVector> health_changed;
};

const PartitionsSnapshotImpl& asImpl(const HostSetSnapshot& snapshot) {
  return static_cast<const PartitionsSnapshotImpl&>(snapshot);
}

// Empty flat views, shared so that dropped flat vectors are released.
const PrioritySet::UpdateHostsParams& emptyViews() {
  CONSTRUCT_ON_FIRST_USE(
      PrioritySet::UpdateHostsParams,
      PrioritySet::UpdateHostsParams{
          std::make_shared<const HostVector>(), std::make_shared<const HealthyHostVector>(),
          std::make_shared<const DegradedHostVector>(),
          std::make_shared<const ExcludedHostVector>(), HostsPerLocalityImpl::empty(),
          HostsPerLocalityImpl::empty(), HostsPerLocalityImpl::empty(),
          HostsPerLocalityImpl::empty(), nullptr});
}

// One partition view, keyed by host. Adding or removing a host is O(log N) and copying the view is
// an O(1) snapshot that later changes do not affect. Iteration order carries no meaning.
class IndexedHosts {
public:
  size_t size() const { return hosts_.size(); }
  bool contains(const Host* host) const { return hosts_.find(host) != nullptr; }
  const HostsByHost& hosts() const { return hosts_; }

  void add(const HostSharedPtr& host) { hosts_.insert(host.get(), host); }

  void remove(const Host* host) { hosts_.erase(host); }

  void setPresent(const HostSharedPtr& host, bool present) {
    if (present) {
      add(host);
    } else {
      remove(host.get());
    }
  }

  void seed(const HostVector& hosts) {
    hosts_ = HostsByHost();
    for (const auto& host : hosts) {
      add(host);
    }
  }

private:
  HostsByHost hosts_;
};

// The healthy, degraded and excluded views of one host population, classified the same way as
// ClusterImplBase::partitionHostList.
struct HealthViews {
  void place(const HostSharedPtr& host) {
    const Host::Health health = host->coarseHealth();
    healthy.setPresent(host, health == Host::Health::Healthy);
    degraded.setPresent(host, health == Host::Health::Degraded);
    excluded.setPresent(host, HostSetImpl::excludedByHealthFlags(*host));
  }

  void remove(const Host* host) {
    healthy.remove(host);
    degraded.remove(host);
    excluded.remove(host);
  }

  IndexedHosts healthy;
  IndexedHosts degraded;
  IndexedHosts excluded;
};

struct LocalityPartitions {
  PersistentViews views() const {
    return {all.hosts(), health.healthy.hosts(), health.degraded.hosts(), health.excluded.hosts()};
  }

  IndexedHosts all;
  HealthViews health;
};

HostVector toHostVector(const HostsByHost& hosts) {
  HostVector out;
  out.reserve(hosts.size());
  hosts.forEach([&out](const Host*, const HostSharedPtr& host) { out.push_back(host); });
  return out;
}

PrioritySet::UpdateHostsParams PartitionsSnapshotImpl::flatUpdateHostsParams() const {
  const PartitionsSnapshotImpl& impl = *this;
  auto hosts = std::make_shared<HostVector>(toHostVector(impl.hosts.all));
  auto healthy_hosts = std::make_shared<HealthyHostVector>(toHostVector(impl.hosts.healthy));
  auto degraded_hosts = std::make_shared<DegradedHostVector>(toHostVector(impl.hosts.degraded));
  auto excluded_hosts = std::make_shared<ExcludedHostVector>(toHostVector(impl.hosts.excluded));

  std::vector<HostVector> all_per_locality;
  std::vector<HostVector> healthy_per_locality;
  std::vector<HostVector> degraded_per_locality;
  std::vector<HostVector> excluded_per_locality;
  for (const PersistentViews& bucket : impl.localities) {
    all_per_locality.push_back(toHostVector(bucket.all));
    healthy_per_locality.push_back(toHostVector(bucket.healthy));
    degraded_per_locality.push_back(toHostVector(bucket.degraded));
    excluded_per_locality.push_back(toHostVector(bucket.excluded));
  }
  const bool local = impl.has_local_locality;
  return PrioritySet::UpdateHostsParams{
      std::move(hosts),
      std::move(healthy_hosts),
      std::move(degraded_hosts),
      std::move(excluded_hosts),
      std::make_shared<HostsPerLocalityImpl>(std::move(all_per_locality), local),
      std::make_shared<HostsPerLocalityImpl>(std::move(healthy_per_locality), local),
      std::make_shared<HostsPerLocalityImpl>(std::move(degraded_per_locality), local),
      std::make_shared<HostsPerLocalityImpl>(std::move(excluded_per_locality), local),
      nullptr};
}

} // namespace

struct PersistentHostSet::Partitions {
  void add(const HostSharedPtr& host) {
    all.add(host);
    health.place(host);
    if (track_localities) {
      LocalityPartitions& bucket = localityFor(host->locality());
      bucket.all.add(host);
      bucket.health.place(host);
    }
  }

  void remove(const HostSharedPtr& host) {
    all.remove(host.get());
    health.remove(host.get());
    if (!track_localities) {
      return;
    }
    const auto it = locality_index.find(host->locality());
    if (it == locality_index.end()) {
      return;
    }
    const size_t index = it->second;
    LocalityPartitions& bucket = *localities[index];
    bucket.all.remove(host.get());
    bucket.health.remove(host.get());
    eraseLocalityIfEmpty(index);
  }

  void reclassifyAll() {
    for (const auto& host : toHostVector(all.hosts())) {
      reclassify(host);
    }
  }

  // Returns false if `host` is not a member.
  bool reclassify(const HostSharedPtr& host) {
    if (!all.contains(host.get())) {
      return false;
    }
    health.place(host);
    if (track_localities) {
      const auto it = locality_index.find(host->locality());
      if (it != locality_index.end()) {
        localities[it->second]->health.place(host);
      }
    }
    return true;
  }

  void seed(const PrioritySet::UpdateHostsParams& flat) {
    all.seed(*flat.hosts);
    health.healthy.seed(flat.healthy_hosts->get());
    health.degraded.seed(flat.degraded_hosts->get());
    health.excluded.seed(flat.excluded_hosts->get());
    localities.clear();
    locality_index.clear();
    has_local_locality = flat.hosts_per_locality->hasLocalLocality();
    const auto& buckets = flat.hosts_per_locality->get();
    // A caller that gives hosts but no per-locality layout does not use localities, so deltas must
    // not invent buckets for it either.
    track_localities = flat.hosts->empty() || !buckets.empty();
    const auto view = [](const HostsPerLocality& per_locality, size_t index) -> const HostVector& {
      static const HostVector empty;
      return index < per_locality.get().size() ? per_locality.get()[index] : empty;
    };
    for (size_t i = 0; i < buckets.size(); ++i) {
      auto bucket = std::make_unique<LocalityPartitions>();
      bucket->all.seed(buckets[i]);
      bucket->health.healthy.seed(view(*flat.healthy_hosts_per_locality, i));
      bucket->health.degraded.seed(view(*flat.degraded_hosts_per_locality, i));
      bucket->health.excluded.seed(view(*flat.excluded_hosts_per_locality, i));
      if (!buckets[i].empty()) {
        locality_index.try_emplace(buckets[i].front()->locality(), i);
      }
      localities.push_back(std::move(bucket));
    }
  }

  std::shared_ptr<PartitionsSnapshotImpl> snapshot() const {
    auto snapshot = std::make_shared<PartitionsSnapshotImpl>();
    snapshot->hosts = {all.hosts(), health.healthy.hosts(), health.degraded.hosts(),
                       health.excluded.hosts()};
    snapshot->localities.reserve(localities.size());
    for (const auto& bucket : localities) {
      snapshot->localities.push_back(bucket->views());
    }
    snapshot->has_local_locality = has_local_locality;
    return snapshot;
  }

  LocalityPartitions& localityFor(const envoy::config::core::v3::Locality& locality) {
    const auto [it, inserted] = locality_index.try_emplace(locality, localities.size());
    if (inserted) {
      localities.push_back(std::make_unique<LocalityPartitions>());
    }
    return *localities[it->second];
  }

  // Drops an emptied bucket, keeping the order of the others. The local locality bucket stays
  // first even when empty, as HostsPerLocality requires.
  void eraseLocalityIfEmpty(size_t index) {
    if (localities[index]->all.size() != 0 || (has_local_locality && index == 0)) {
      return;
    }
    localities.erase(localities.begin() + index);
    absl::erase_if(locality_index, [index](const auto& entry) { return entry.second == index; });
    for (auto& [_, bucket_index] : locality_index) {
      if (bucket_index > index) {
        --bucket_index;
      }
    }
  }

  IndexedHosts all;
  HealthViews health;
  std::vector<std::unique_ptr<LocalityPartitions>> localities;
  absl::flat_hash_map<envoy::config::core::v3::Locality, size_t, Upstream::LocalityHash,
                      Upstream::LocalityEqualTo>
      locality_index;
  bool has_local_locality{false};
  bool track_localities{true};
};

PersistentHostSet::PersistentHostSet(uint32_t priority,
                                     std::optional<bool> weighted_priority_health,
                                     std::optional<uint32_t> overprovisioning_factor)
    : HostSetImpl(priority, weighted_priority_health, overprovisioning_factor),
      partitions_(std::make_unique<Partitions>()), flat_(emptyViews()) {}

PersistentHostSet::~PersistentHostSet() = default;

HostCounts PersistentHostSet::hostCounts() const {
  if (source_ == Source::Partitions) {
    return {partitions_->all.size(), partitions_->health.healthy.size(),
            partitions_->health.degraded.size(), partitions_->health.excluded.size()};
  }
  if (source_ == Source::Snapshot) {
    const auto& views = asImpl(*adopted_snapshot_).hosts;
    return {views.all.size(), views.healthy.size(), views.degraded.size(), views.excluded.size()};
  }
  return {flat_.hosts->size(), flat_.healthy_hosts->get().size(),
          flat_.degraded_hosts->get().size(), flat_.excluded_hosts->get().size()};
}

HostSetSnapshotConstSharedPtr PersistentHostSet::snapshot() const { return currentSnapshot(); }

void PersistentHostSet::updateHosts(PrioritySet::UpdateHostsParams&& update_hosts_params,
                                    LocalityWeightsConstSharedPtr locality_weights,
                                    const HostVector& hosts_added, const HostVector& hosts_removed,
                                    std::optional<bool> weighted_priority_health,
                                    std::optional<uint32_t> overprovisioning_factor) {
  // With a snapshot the base leaves its flat views alone; this set serves them from the snapshot.
  if (update_hosts_params.snapshot != nullptr) {
    // Unless it is the snapshot of a change to `partitions_`, which stay current, adopt it.
    if (update_hosts_params.snapshot != partitions_snapshot_) {
      partitions_snapshot_ = nullptr;
      adopted_snapshot_ = update_hosts_params.snapshot;
      health_changed_ = asImpl(*adopted_snapshot_).health_changed;
      source_ = Source::Snapshot;
      flat_ = emptyViews();
      flat_built_ = false;
    }
  } else {
    partitions_snapshot_ = nullptr;
    adopted_snapshot_ = nullptr;
    health_changed_ = std::nullopt;
    source_ = Source::Flat;
    flat_ = update_hosts_params;
    flat_built_ = true;
  }
  HostSetImpl::updateHosts(std::move(update_hosts_params), std::move(locality_weights), hosts_added,
                           hosts_removed, weighted_priority_health, overprovisioning_factor);
}

HostSetSnapshotConstSharedPtr
PersistentHostSet::applyMembershipDelta(const HostVector& hosts_added,
                                        const HostVector& hosts_removed) {
  ENVOY_BUG(localityWeights() == nullptr,
            "locality weights are not supported with persistent host partitions");
  syncPartitions();
  for (const auto& host : hosts_removed) {
    partitions_->remove(host);
  }
  for (const auto& host : hosts_added) {
    partitions_->add(host);
  }
  // Added hosts arrive with their current health, so no other host's health changed.
  return partitionsChanged(HostVector{});
}

HostSetSnapshotConstSharedPtr PersistentHostSet::applyHostHealthChange(const HostSharedPtr& host) {
  syncPartitions();
  if (!partitions_->reclassify(host)) {
    return nullptr;
  }
  return partitionsChanged(HostVector{host});
}

HostSetSnapshotConstSharedPtr PersistentHostSet::refreshHealth() {
  syncPartitions();
  partitions_->reclassifyAll();
  // Any host may have changed health, so the load balancer rereads them all.
  return partitionsChanged(std::nullopt);
}

const PrioritySet::UpdateHostsParams& PersistentHostSet::flat() const {
  if (!flat_built_) {
    flat_ = asImpl(*currentSnapshot()).flatUpdateHostsParams();
    flat_built_ = true;
  }
  return flat_;
}

HostSetSnapshotConstSharedPtr PersistentHostSet::currentSnapshot() const {
  if (source_ == Source::Snapshot) {
    return adopted_snapshot_;
  }
  if (source_ == Source::Flat) {
    return nullptr;
  }
  if (partitions_snapshot_ == nullptr) {
    auto snapshot = partitions_->snapshot();
    snapshot->health_changed = health_changed_;
    partitions_snapshot_ = std::move(snapshot);
  }
  return partitions_snapshot_;
}

void PersistentHostSet::syncPartitions() {
  if (source_ == Source::Partitions) {
    return;
  }
  // flat() is current in both other states, built from the adopted snapshot if necessary.
  partitions_->seed(flat());
  source_ = Source::Partitions;
  adopted_snapshot_ = nullptr;
  partitions_snapshot_ = nullptr;
}

HostSetSnapshotConstSharedPtr
PersistentHostSet::partitionsChanged(std::optional<HostVector> health_changed) {
  health_changed_ = std::move(health_changed);
  source_ = Source::Partitions;
  partitions_snapshot_ = nullptr;
  flat_ = emptyViews();
  flat_built_ = false;
  return currentSnapshot();
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
