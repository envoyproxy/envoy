#include "common/upstream/subset_lb.h"

#include <unordered_set>

#include "envoy/runtime/runtime.h"

#include "common/common/assert.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/utility.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/ring_hash_lb.h"

#include "api/cds.pb.h"

namespace Envoy {
namespace Upstream {

SubsetLoadBalancer::SubsetLoadBalancer(
    LoadBalancerType lb_type, PrioritySet& priority_set, const PrioritySet* local_priority_set,
    ClusterStats& stats, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const LoadBalancerSubsetInfo& subsets,
    const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& lb_ring_hash_config)
    : lb_type_(lb_type), lb_ring_hash_config_(lb_ring_hash_config), stats_(stats),
      runtime_(runtime), random_(random), fallback_policy_(subsets.fallbackPolicy()),
      default_subset_(subsets.defaultSubset()), subset_keys_(subsets.subsetKeys()),
      original_priority_set_(priority_set), original_local_priority_set_(local_priority_set) {
  ASSERT(subsets.isEnabled());

  // Create filtered default subset (if necessary) and other subsets based on current hosts.
  for (auto& host_set : priority_set.hostSetsPerPriority()) {
    update(host_set->priority(), host_set->hosts(), {});
  }

  // Configure future updates.
  priority_set.addMemberUpdateCb([this](uint32_t priority,
                                        const std::vector<HostSharedPtr>& hosts_added,
                                        const std::vector<HostSharedPtr>& hosts_removed) {
    update(priority, hosts_added, hosts_removed);
  });
}

HostConstSharedPtr SubsetLoadBalancer::chooseHost(LoadBalancerContext* context) {
  if (context) {
    bool host_chosen;
    HostConstSharedPtr host = tryChooseHostFromContext(context, host_chosen);
    if (host_chosen) {
      // Subset lookup succeeded, return this result even if it's nullptr.
      return host;
    }
  }

  if (fallback_subset_ == nullptr) {
    return nullptr;
  }

  stats_.lb_subsets_fallback_.inc();
  return fallback_subset_->priority_subset_->lb_->chooseHost(context);
}

// Find a host from the subsets. Sets host_chosen to false and returns nullptr if the context has
// no metadata match criteria, if there is no matching subset, or if the matching subset contains
// no hosts (ignoring health). Otherwise, host_chosen is true and the returns HostConstSharedPtr is
// from the subset's load balancer (technically, it may still be nullptr).
HostConstSharedPtr SubsetLoadBalancer::tryChooseHostFromContext(LoadBalancerContext* context,
                                                                bool& host_chosen) {
  host_chosen = false;
  const Router::MetadataMatchCriteria* match_criteria = context->metadataMatchCriteria();
  if (!match_criteria) {
    return nullptr;
  }

  // Route has metadata match criteria defined, see if we have a matching subset.
  LbSubsetEntryPtr entry = findSubset(match_criteria->metadataMatchCriteria());
  if (entry == nullptr || !entry->active()) {
    // No matching subset or subset not active: use fallback policy.
    return nullptr;
  }

  host_chosen = true;
  stats_.lb_subsets_selected_.inc();
  return entry->priority_subset_->lb_->chooseHost(context);
}

// Iterates over the given metadata match criteria (which must be lexically sorted by key) and find
// a matching LbSubsetEnryPtr, if any.
SubsetLoadBalancer::LbSubsetEntryPtr SubsetLoadBalancer::findSubset(
    const std::vector<Router::MetadataMatchCriterionConstSharedPtr>& match_criteria) {
  const LbSubsetMap* subsets = &subsets_;

  // Because the match_criteria and the host metadata used to populate subsets_ are sorted in the
  // same order, we can iterate over the criteria and perform a lookup for each key and value,
  // starting with the root LbSubsetMap and using the previous iteration's LbSubsetMap thereafter
  // (tracked in subsets). If ever a criterion's key or value is not found, there is no subset for
  // this criteria. If we reach the last criterion, we've found the LbSubsetEntry for the criteria,
  // which may or may not have a subset attached to it.
  for (uint32_t i = 0; i < match_criteria.size(); i++) {
    const Router::MetadataMatchCriterion& match_criterion = *match_criteria[i];
    const auto& subset_it = subsets->find(match_criterion.name());
    if (subset_it == subsets->end()) {
      // No subsets with this key (at this level in the hierarchy).
      break;
    }

    const ValueSubsetMap& vs_map = subset_it->second;
    const auto& vs_it = vs_map.find(match_criterion.value());
    if (vs_it == vs_map.end()) {
      // No subsets with this value.
      break;
    }

    const LbSubsetEntryPtr& entry = vs_it->second;
    if (i + 1 == match_criteria.size()) {
      // We've reached the end of the criteria, and they all matched.
      return entry;
    }

    subsets = &entry->children_;
  }

  return nullptr;
}

void SubsetLoadBalancer::updateFallbackSubset(uint32_t priority,
                                              const std::vector<HostSharedPtr>& hosts_added,
                                              const std::vector<HostSharedPtr>& hosts_removed) {
  if (fallback_policy_ == envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK) {
    return;
  }

  HostPredicate predicate;

  if (fallback_policy_ == envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT) {
    predicate = [](const Host&) -> bool { return true; };
  } else {
    predicate =
        std::bind(&SubsetLoadBalancer::hostMatchesDefaultSubset, this, std::placeholders::_1);
  }

  if (fallback_subset_ == nullptr) {
    // First update: create the default host subset.
    fallback_subset_.reset(new LbSubsetEntry());
    fallback_subset_->priority_subset_.reset(new PrioritySubsetImpl(*this, predicate));
  } else {
    // Subsequent updates: add/remove hosts.
    fallback_subset_->priority_subset_->update(priority, hosts_added, hosts_removed, predicate);
  }
}

// Iterates over the added and removed hosts, looking up an LbSubsetEntryPtr for each. For every
// unique LbSubsetEntryPtr found, it invokes cb with the LbSubsetEntryPtr, a HostPredicate that
// selects hosts in the subset, and a flag indicating whether any hosts are being added.
void SubsetLoadBalancer::processSubsets(
    const std::vector<HostSharedPtr>& hosts_added, const std::vector<HostSharedPtr>& hosts_removed,
    std::function<void(LbSubsetEntryPtr, HostPredicate, bool)> cb) {
  std::unordered_set<LbSubsetEntryPtr> subsets_modified;

  std::pair<const std::vector<HostSharedPtr>&, bool> steps[] = {{hosts_added, true},
                                                                {hosts_removed, false}};
  for (const auto& step : steps) {
    const auto& hosts = step.first;
    const bool adding_hosts = step.second;

    for (const auto& host : hosts) {
      for (const auto& keys : subset_keys_) {
        // For each host, for each subset key, attempt to extract the metadata corresponding to the
        // key from the host.
        SubsetMetadata kvs = extractSubsetMetadata(keys, *host);
        if (!kvs.empty()) {
          // The host has metadata for each key, find or create its subset.
          LbSubsetEntryPtr entry = findOrCreateSubset(subsets_, kvs, 0);
          if (subsets_modified.find(entry) != subsets_modified.end()) {
            // We've already invoked the callback for this entry.
            continue;
          }
          subsets_modified.emplace(entry);

          HostPredicate predicate =
              std::bind(&SubsetLoadBalancer::hostMatches, this, kvs, std::placeholders::_1);

          cb(entry, predicate, adding_hosts);
        }
      }
    }
  }
}

// Given the addition and/or removal of hosts, update all subsets for this priority level, creating
// new subsets as necessary.
void SubsetLoadBalancer::update(uint32_t priority, const std::vector<HostSharedPtr>& hosts_added,
                                const std::vector<HostSharedPtr>& hosts_removed) {
  updateFallbackSubset(priority, hosts_added, hosts_removed);

  processSubsets(hosts_added, hosts_removed,
                 [&](LbSubsetEntryPtr entry, HostPredicate predicate, bool adding_host) {
                   if (entry->initialized()) {
                     const bool active_before = entry->active();
                     entry->priority_subset_->update(priority, hosts_added, hosts_removed,
                                                     predicate);

                     if (active_before && !entry->active()) {
                       stats_.lb_subsets_active_.dec();
                       stats_.lb_subsets_removed_.inc();
                     } else if (!active_before && entry->active()) {
                       stats_.lb_subsets_active_.inc();
                       stats_.lb_subsets_created_.inc();
                     }
                   } else if (adding_host) {
                     // Initialize new entry with hosts and update stats. (An uninitialized entry
                     // with only removed hosts is a degenerate case and we leave the entry
                     // uninitialized.)
                     entry->priority_subset_.reset(new PrioritySubsetImpl(*this, predicate));
                     stats_.lb_subsets_active_.inc();
                     stats_.lb_subsets_created_.inc();
                   }
                 });
}

bool SubsetLoadBalancer::hostMatchesDefaultSubset(const Host& host) {
  const envoy::api::v2::Metadata& host_metadata = host.metadata();

  for (const auto& it : default_subset_.fields()) {
    const ProtobufWkt::Value& host_value = Config::Metadata::metadataValue(
        host_metadata, Config::MetadataFilters::get().ENVOY_LB, it.first);

    if (!ValueUtil::equal(host_value, it.second)) {
      return false;
    }
  }

  return true;
}

bool SubsetLoadBalancer::hostMatches(const SubsetMetadata& kvs, const Host& host) {
  const envoy::api::v2::Metadata& host_metadata = host.metadata();

  for (const auto& kv : kvs) {
    const ProtobufWkt::Value& host_value = Config::Metadata::metadataValue(
        host_metadata, Config::MetadataFilters::get().ENVOY_LB, kv.first);

    if (!ValueUtil::equal(host_value, kv.second)) {
      return false;
    }
  }

  return true;
}

// Iterates over subset_keys looking up values from the given host's metadata. Each key-value pair
// is appended to kvs. Returns a non-empty value if the host has a value for each key.
SubsetLoadBalancer::SubsetMetadata
SubsetLoadBalancer::extractSubsetMetadata(const std::set<std::string>& subset_keys,
                                          const Host& host) {
  SubsetMetadata kvs;

  const envoy::api::v2::Metadata& metadata = host.metadata();
  const auto& filter_it = metadata.filter_metadata().find(Config::MetadataFilters::get().ENVOY_LB);
  if (filter_it == metadata.filter_metadata().end()) {
    return kvs;
  }

  const auto& fields = filter_it->second.fields();
  for (const auto key : subset_keys) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
      break;
    }
    kvs.emplace_back(std::pair<std::string, ProtobufWkt::Value>(key, it->second));
  }

  if (kvs.size() != subset_keys.size()) {
    kvs.clear();
  }

  return kvs;
}

// Given a vector of key-values (from extractSubsetMetadata), recursively finds the matching
// LbSubsetEntryPtr.
SubsetLoadBalancer::LbSubsetEntryPtr
SubsetLoadBalancer::findOrCreateSubset(LbSubsetMap& subsets, const SubsetMetadata& kvs,
                                       uint32_t idx) {
  ASSERT(idx < kvs.size());

  const std::string& name = kvs[idx].first;
  const ProtobufWkt::Value& pb_value = kvs[idx].second;
  const HashedValue value(pb_value);
  LbSubsetEntryPtr entry;

  const auto& kv_it = subsets.find(name);
  if (kv_it != subsets.end()) {
    ValueSubsetMap& value_subset_map = kv_it->second;
    const auto vs_it = value_subset_map.find(value);
    if (vs_it != value_subset_map.end()) {
      entry = vs_it->second;
    }
  }

  if (!entry) {
    // Not found. Create an uninitialized entry.
    entry.reset(new LbSubsetEntry());
    if (kv_it != subsets.end()) {
      ValueSubsetMap& value_subset_map = kv_it->second;
      value_subset_map.emplace(value, entry);
    } else {
      ValueSubsetMap value_subset_map = {{value, entry}};
      subsets.emplace(name, value_subset_map);
    }
  }

  idx++;
  if (idx == kvs.size()) {
    // We've matched all the key-values, return the entry.
    return entry;
  }

  return findOrCreateSubset(entry->children_, kvs, idx);
}

// Initialize a new HostSubsetImpl and LoadBalancer from the SubsetLoadBalancer, filtering hosts
// with the given predicate.
SubsetLoadBalancer::PrioritySubsetImpl::PrioritySubsetImpl(const SubsetLoadBalancer& subset_lb,
                                                           HostPredicate predicate)
    : PrioritySetImpl(), original_priority_set_(subset_lb.original_priority_set_) {

  for (size_t i = 0; i < original_priority_set_.hostSetsPerPriority().size(); ++i) {
    empty_ &= getOrCreateHostSet(i).hosts().empty();
  }

  for (size_t i = 0; i < subset_lb.original_priority_set_.hostSetsPerPriority().size(); ++i) {
    update(i, subset_lb.original_priority_set_.hostSetsPerPriority()[i]->hosts(), {}, predicate);
  }

  switch (subset_lb.lb_type_) {
  case LoadBalancerType::LeastRequest:
    lb_.reset(new LeastRequestLoadBalancer(*this, subset_lb.original_local_priority_set_,
                                           subset_lb.stats_, subset_lb.runtime_,
                                           subset_lb.random_));
    break;

  case LoadBalancerType::Random:
    lb_.reset(new RandomLoadBalancer(*this, subset_lb.original_local_priority_set_,
                                     subset_lb.stats_, subset_lb.runtime_, subset_lb.random_));
    break;

  case LoadBalancerType::RoundRobin:
    lb_.reset(new RoundRobinLoadBalancer(*this, subset_lb.original_local_priority_set_,
                                         subset_lb.stats_, subset_lb.runtime_, subset_lb.random_));
    break;

  case LoadBalancerType::RingHash:
    // TODO(mattklein123): The ring hash LB is thread aware, but currently the subset LB is not.
    // We should make the subset LB thread aware since the calculations are costly, and then we
    // can also use a thread aware sub-LB properly. The following works fine but is not optimal.
    thread_aware_lb_.reset(new RingHashLoadBalancer(*this, subset_lb.stats_, subset_lb.runtime_,
                                                    subset_lb.random_,
                                                    subset_lb.lb_ring_hash_config_));
    thread_aware_lb_->initialize();
    lb_ = thread_aware_lb_->factory()->create();
    break;

  case LoadBalancerType::OriginalDst:
    NOT_REACHED;
  }

  triggerCallbacks();
}

// Given hosts_added and hosts_removed, update the underlying HostSet. The hosts_added Hosts must
// be filtered to match hosts that belong in this subset. The hosts_removed Hosts are ignored if
// they are not currently a member of this subset.
void SubsetLoadBalancer::HostSubsetImpl::update(const std::vector<HostSharedPtr>& hosts_added,
                                                const std::vector<HostSharedPtr>& hosts_removed,
                                                std::function<bool(const Host&)> predicate) {
  std::vector<HostSharedPtr> filtered_added;
  for (const auto host : hosts_added) {
    if (predicate(*host)) {
      filtered_added.emplace_back(host);
    }
  }

  std::vector<HostSharedPtr> filtered_removed;
  for (const auto host : hosts_removed) {
    if (predicate(*host)) {
      filtered_removed.emplace_back(host);
    }
  }

  if (filtered_added.empty() && filtered_removed.empty()) {
    return;
  }

  HostVectorSharedPtr hosts(new std::vector<HostSharedPtr>());
  HostVectorSharedPtr healthy_hosts(new std::vector<HostSharedPtr>());
  HostListsSharedPtr hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>());
  HostListsSharedPtr healthy_hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>());

  for (const auto host : original_host_set_.hosts()) {
    if (predicate(*host)) {
      hosts->emplace_back(host);
      if (host->healthy()) {
        healthy_hosts->emplace_back(host);
      }
    }
  }

  for (const auto locality_hosts : original_host_set_.hostsPerLocality()) {
    std::vector<HostSharedPtr> curr_locality_hosts;
    std::vector<HostSharedPtr> curr_locality_healthy_hosts;
    for (const auto locality_host : locality_hosts) {
      if (predicate(*locality_host)) {
        curr_locality_hosts.emplace_back(locality_host);
        if (locality_host->healthy()) {
          curr_locality_healthy_hosts.emplace_back(locality_host);
        }
      }
    }

    hosts_per_locality->emplace_back(curr_locality_hosts);
    healthy_hosts_per_locality->emplace_back(curr_locality_healthy_hosts);
  }

  HostSetImpl::updateHosts(hosts, healthy_hosts, hosts_per_locality, healthy_hosts_per_locality,
                           filtered_added, filtered_removed);
}

HostSetImplPtr SubsetLoadBalancer::PrioritySubsetImpl::createHostSet(uint32_t priority) {
  RELEASE_ASSERT(priority < original_priority_set_.hostSetsPerPriority().size());
  return HostSetImplPtr{
      new HostSubsetImpl(*original_priority_set_.hostSetsPerPriority()[priority])};
}

void SubsetLoadBalancer::PrioritySubsetImpl::update(uint32_t priority,
                                                    const std::vector<HostSharedPtr>& hosts_added,
                                                    const std::vector<HostSharedPtr>& hosts_removed,
                                                    std::function<bool(const Host&)> predicate) {
  HostSubsetImpl* host_subset = getOrCreateHostSubset(priority);
  host_subset->update(hosts_added, hosts_removed, predicate);

  if (host_subset->hosts().empty() != empty_) {
    empty_ = true;
    for (auto& host_set : hostSetsPerPriority()) {
      empty_ &= host_set->hosts().empty();
    }
  }

  // Create a new worker local LB if needed.
  // TODO(mattklein123): See the PrioritySubsetImpl constructor for additional comments on how
  // we can do better here.
  if (thread_aware_lb_ != nullptr) {
    lb_ = thread_aware_lb_->factory()->create();
  }
}

} // namespace Upstream
} // namespace Envoy
