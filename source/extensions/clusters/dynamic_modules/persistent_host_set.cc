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
using Upstream::DegradedHostVectorConstSharedPtr;
using Upstream::ExcludedHostVector;
using Upstream::ExcludedHostVectorConstSharedPtr;
using Upstream::HealthyHostVector;
using Upstream::HealthyHostVectorConstSharedPtr;
using Upstream::Host;
using Upstream::HostCounts;
using Upstream::HostSetImpl;
using Upstream::HostSetSnapshot;
using Upstream::HostSetSnapshotConstSharedPtr;
using Upstream::HostSharedPtr;
using Upstream::HostsPerLocalityConstSharedPtr;
using Upstream::HostsPerLocalityImpl;
using Upstream::HostVector;
using Upstream::HostVectorConstSharedPtr;
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
  PersistentViews hosts;
  std::vector<PersistentViews> localities;
  // See PersistentHostSet::healthChangedHosts().
  std::optional<HostsByHost> health_changed;
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

HostVector toHostVector(const HostsByHost& hosts) {
  HostVector out;
  out.reserve(hosts.size());
  hosts.forEach([&out](const Host*, const HostSharedPtr& host) { out.push_back(host); });
  return out;
}

// Builds one per-locality flat view from the snapshot's buckets.
HostsPerLocalityConstSharedPtr perLocality(const std::vector<PersistentViews>& localities,
                                           HostsByHost PersistentViews::* view) {
  std::vector<HostVector> buckets;
  buckets.reserve(localities.size());
  for (const PersistentViews& bucket : localities) {
    buckets.push_back(toHostVector(bucket.*view));
  }
  return std::make_shared<HostsPerLocalityImpl>(std::move(buckets), false);
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

} // namespace

struct PersistentHostSet::Partitions {
  void add(const HostSharedPtr& host) {
    all.add(host);
    health.place(host);
    LocalityPartitions& bucket = localityFor(host->locality());
    bucket.all.add(host);
    bucket.health.place(host);
  }

  void remove(const HostSharedPtr& host) {
    all.remove(host.get());
    health.remove(host.get());
    if (health_changed.has_value()) {
      health_changed->erase(host.get());
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
    all.hosts().forEach([this](const Host*, const HostSharedPtr& host) { reclassify(host); });
  }

  // Returns false if `host` is not a member.
  bool reclassify(const HostSharedPtr& host) {
    if (!all.contains(host.get())) {
      return false;
    }
    health.place(host);
    const auto it = locality_index.find(host->locality());
    if (it != locality_index.end()) {
      localities[it->second]->health.place(host);
    }
    return true;
  }

  std::shared_ptr<PartitionsSnapshotImpl> snapshot() const {
    auto snapshot = std::make_shared<PartitionsSnapshotImpl>();
    snapshot->hosts = {all.hosts(), health.healthy.hosts(), health.degraded.hosts(),
                       health.excluded.hosts()};
    snapshot->localities.reserve(localities.size());
    for (const auto& bucket : localities) {
      snapshot->localities.push_back(bucket->views());
    }
    snapshot->health_changed = health_changed;
    return snapshot;
  }

  LocalityPartitions& localityFor(const envoy::config::core::v3::Locality& locality) {
    const auto [it, inserted] = locality_index.try_emplace(locality, localities.size());
    if (inserted) {
      localities.push_back(std::make_unique<LocalityPartitions>());
    }
    return *localities[it->second];
  }

  // Drops an emptied bucket, keeping the order of the others.
  void eraseLocalityIfEmpty(size_t index) {
    if (localities[index]->all.size() != 0) {
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
  // Buckets are created by the first host of a locality and dropped with the last one, so none is
  // empty and there is no local locality.
  std::vector<std::unique_ptr<LocalityPartitions>> localities;
  absl::flat_hash_map<envoy::config::core::v3::Locality, size_t, Upstream::LocalityHash,
                      Upstream::LocalityEqualTo>
      locality_index;
  // Members whose health changed since the last hand-out, or nullopt when that is unknown. Removed
  // hosts leave it, and a host flipped twice is listed once.
  std::optional<HostsByHost> health_changed{std::in_place};
};

PersistentHostSet::PersistentHostSet(uint32_t priority,
                                     std::optional<bool> weighted_priority_health,
                                     std::optional<uint32_t> overprovisioning_factor)
    : HostSetImpl(priority, weighted_priority_health, overprovisioning_factor),
      partitions_(std::make_unique<Partitions>()), flat_(emptyViews()) {}

PersistentHostSet::~PersistentHostSet() = default;

HostVectorConstSharedPtr PersistentHostSet::hostsPtr() const {
  if (flat_.hosts == nullptr) {
    flat_.hosts =
        std::make_shared<const HostVector>(toHostVector(asImpl(*currentSnapshot()).hosts.all));
  }
  return flat_.hosts;
}

HealthyHostVectorConstSharedPtr PersistentHostSet::healthyHostsPtr() const {
  if (flat_.healthy_hosts == nullptr) {
    flat_.healthy_hosts = std::make_shared<const HealthyHostVector>(
        toHostVector(asImpl(*currentSnapshot()).hosts.healthy));
  }
  return flat_.healthy_hosts;
}

DegradedHostVectorConstSharedPtr PersistentHostSet::degradedHostsPtr() const {
  if (flat_.degraded_hosts == nullptr) {
    flat_.degraded_hosts = std::make_shared<const DegradedHostVector>(
        toHostVector(asImpl(*currentSnapshot()).hosts.degraded));
  }
  return flat_.degraded_hosts;
}

ExcludedHostVectorConstSharedPtr PersistentHostSet::excludedHostsPtr() const {
  if (flat_.excluded_hosts == nullptr) {
    flat_.excluded_hosts = std::make_shared<const ExcludedHostVector>(
        toHostVector(asImpl(*currentSnapshot()).hosts.excluded));
  }
  return flat_.excluded_hosts;
}

HostsPerLocalityConstSharedPtr PersistentHostSet::hostsPerLocalityPtr() const {
  if (flat_.hosts_per_locality == nullptr) {
    flat_.hosts_per_locality =
        perLocality(asImpl(*currentSnapshot()).localities, &PersistentViews::all);
  }
  return flat_.hosts_per_locality;
}

HostsPerLocalityConstSharedPtr PersistentHostSet::healthyHostsPerLocalityPtr() const {
  if (flat_.healthy_hosts_per_locality == nullptr) {
    flat_.healthy_hosts_per_locality =
        perLocality(asImpl(*currentSnapshot()).localities, &PersistentViews::healthy);
  }
  return flat_.healthy_hosts_per_locality;
}

HostsPerLocalityConstSharedPtr PersistentHostSet::degradedHostsPerLocalityPtr() const {
  if (flat_.degraded_hosts_per_locality == nullptr) {
    flat_.degraded_hosts_per_locality =
        perLocality(asImpl(*currentSnapshot()).localities, &PersistentViews::degraded);
  }
  return flat_.degraded_hosts_per_locality;
}

HostsPerLocalityConstSharedPtr PersistentHostSet::excludedHostsPerLocalityPtr() const {
  if (flat_.excluded_hosts_per_locality == nullptr) {
    flat_.excluded_hosts_per_locality =
        perLocality(asImpl(*currentSnapshot()).localities, &PersistentViews::excluded);
  }
  return flat_.excluded_hosts_per_locality;
}

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

HostSetSnapshotConstSharedPtr PersistentHostSet::snapshot() const {
  if (source_ == Source::Partitions) {
    health_changes_handed_out_ = true;
  }
  return currentSnapshot();
}

std::optional<HostVector> PersistentHostSet::healthChangedHosts() const {
  const auto snapshot = currentSnapshot();
  if (snapshot == nullptr || !asImpl(*snapshot).health_changed.has_value()) {
    return std::nullopt;
  }
  return toHostVector(*asImpl(*snapshot).health_changed);
}

void PersistentHostSet::forEachHealthyHost(absl::FunctionRef<void(const HostSharedPtr&)> fn) const {
  const auto snapshot = currentSnapshot();
  if (snapshot == nullptr) {
    for (const auto& host : flat_.healthy_hosts->get()) {
      fn(host);
    }
    return;
  }
  asImpl(*snapshot).hosts.healthy.forEach(
      [&fn](const Host*, const HostSharedPtr& host) { fn(host); });
}

void PersistentHostSet::updateHosts(PrioritySet::UpdateHostsParams&& update_hosts_params,
                                    LocalityWeightsConstSharedPtr locality_weights,
                                    const HostVector& hosts_added, const HostVector& hosts_removed,
                                    std::optional<bool> weighted_priority_health,
                                    std::optional<uint32_t> overprovisioning_factor) {
  // With a snapshot the base leaves its flat views alone; this set serves them from the snapshot.
  if (update_hosts_params.snapshot != nullptr) {
    if (update_hosts_params.snapshot == partitions_snapshot_) {
      // The snapshot of a change to `partitions_`, which stay current.
      change_pending_ = false;
    } else {
      partitions_snapshot_ = nullptr;
      adopted_snapshot_ = update_hosts_params.snapshot;
      source_ = Source::Snapshot;
      flat_ = {};
    }
  } else {
    partitions_snapshot_ = nullptr;
    adopted_snapshot_ = nullptr;
    source_ = Source::Flat;
    flat_ = update_hosts_params;
    change_pending_ = false;
  }
  HostSetImpl::updateHosts(std::move(update_hosts_params), std::move(locality_weights), hosts_added,
                           hosts_removed, weighted_priority_health, overprovisioning_factor);
}

HostSetSnapshotConstSharedPtr
PersistentHostSet::applyMembershipDelta(const HostVector& hosts_added,
                                        const HostVector& hosts_removed) {
  ENVOY_BUG(localityWeights() == nullptr,
            "locality weights are not supported with persistent host partitions");
  beginChange();
  for (const auto& host : hosts_removed) {
    partitions_->remove(host);
  }
  // Added hosts arrive with their current health, so they are not health changes.
  for (const auto& host : hosts_added) {
    partitions_->add(host);
  }
  return partitionsChanged();
}

HostSetSnapshotConstSharedPtr PersistentHostSet::applyHostHealthChange(const HostSharedPtr& host) {
  beginChange();
  if (!partitions_->reclassify(host)) {
    return nullptr;
  }
  if (partitions_->health_changed.has_value()) {
    partitions_->health_changed->insert(host.get(), host);
  }
  return partitionsChanged();
}

HostSetSnapshotConstSharedPtr PersistentHostSet::refreshHealth() {
  beginChange();
  partitions_->reclassifyAll();
  // Any host may have changed health, so the load balancer rereads them all.
  partitions_->health_changed = std::nullopt;
  return partitionsChanged();
}

HostSetSnapshotConstSharedPtr PersistentHostSet::currentSnapshot() const {
  if (source_ == Source::Snapshot) {
    return adopted_snapshot_;
  }
  if (source_ == Source::Flat) {
    return nullptr;
  }
  if (partitions_snapshot_ == nullptr) {
    partitions_snapshot_ = partitions_->snapshot();
  }
  return partitions_snapshot_;
}

void PersistentHostSet::beginChange() {
  ENVOY_BUG(!change_pending_,
            "persistent host set changed before its last snapshot reached updateHosts()");
  if (source_ != Source::Partitions) {
    // Only the main thread's set changes, and it starts empty and never receives a non-empty flat
    // update.
    ENVOY_BUG(source_ == Source::Flat && flat_.hosts->empty(),
              "persistent host set patched after a non-empty full update or on a worker");
    partitions_ = std::make_unique<Partitions>();
    adopted_snapshot_ = nullptr;
    partitions_snapshot_ = nullptr;
    source_ = Source::Partitions;
  } else if (health_changes_handed_out_) {
    partitions_->health_changed.emplace();
  }
  health_changes_handed_out_ = false;
}

HostSetSnapshotConstSharedPtr PersistentHostSet::partitionsChanged() {
  source_ = Source::Partitions;
  partitions_snapshot_ = nullptr;
  flat_ = {};
  change_pending_ = true;
  return currentSnapshot();
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
