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

const HostSetImpl SubsetLoadBalancer::empty_host_set_;

SubsetLoadBalancer::SubsetLoadBalancer(LoadBalancerType lb_type, HostSet& host_set,
                                       const HostSet* local_host_set, ClusterStats& stats,
                                       Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                       const LoadBalancerSubsetInfo& subsets)
    : lb_type_(lb_type), stats_(stats), runtime_(runtime), random_(random),
      fallback_policy_(subsets.fallbackPolicy()), default_subset_(subsets.defaultSubset()),
      subset_keys_(subsets.subsetKeys()), original_host_set_(host_set),
      original_local_host_set_(local_host_set) {
  RELEASE_ASSERT(subsets.isEnabled());

  if (fallback_policy_ == envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET &&
      default_subset_.fields_size() == 0) {
    // All hosts will match, so don't bother checking.
    fallback_policy_ = envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT;
  }

  // Create filtered default subset (if necessary) and other subsets based on current hosts.
  update(host_set.hosts(), {}, false);

  // Configure future updates.
  host_set.addMemberUpdateCb([this](const std::vector<HostSharedPtr>& hosts_added,
                                    const std::vector<HostSharedPtr>& hosts_removed) -> void {
    update(hosts_added, hosts_removed, false);
  });

  if (local_host_set) {
    // No need to invoke update with local = true: subset creation handles the local hosts.
    local_host_set->addMemberUpdateCb(
        [this](const std::vector<HostSharedPtr>& hosts_added,
               const std::vector<HostSharedPtr>& hosts_removed) -> void {
          update(hosts_added, hosts_removed, true);
        });
  }
}

HostConstSharedPtr SubsetLoadBalancer::chooseHost(const LoadBalancerContext* context) {
  if (context) {
    const Router::MetadataMatchCriteria* matches = context->metadataMatchCriteria();
    if (matches) {
      // Route has metadata_matches defined, see if we have a matching subset.
      SubsetPtr subset = findSubset(matches->metadataMatchCriteria());
      if (subset != nullptr && subset->lb_ != nullptr) {
        HostConstSharedPtr host = subset->lb_->chooseHost(context);
        // Fallback if the subset was empty (inactive), but not, for
        // example, if all its hosts are unhealthy.
        if (host || !subset->empty()) {
          stats_.lb_subsets_selected_.inc();
          return host;
        }
      }
    }
  }

  if (default_host_subset_ == nullptr) {
    return nullptr;
  }

  stats_.lb_subsets_fallback_.inc();
  return default_host_subset_->lb_->chooseHost(context);
}

// Iterates over the given metadata matches (which must be lexically
// sorted by key) and find a matching SubsetPtr, if any.
SubsetLoadBalancer::SubsetPtr SubsetLoadBalancer::findSubset(
    const std::vector<Router::MetadataMatchCriterionConstSharedPtr>& matches) {
  LbSubsetMap* subsets = &subsets_;
  for (size_t i = 0; i < matches.size(); i++) {
    const Router::MetadataMatchCriterionConstSharedPtr& match = matches[i];
    const auto& subset_it = subsets->find(match->name());
    if (subset_it == subsets->end()) {
      // No subsets with this key (at this level in the hierarchy).
      break;
    }

    const ValueSubsetMap& vs_map = subset_it->second;
    const auto& vs_it = vs_map.find(match->value());
    if (vs_it == vs_map.end()) {
      // No subsets with this value.
      break;
    }

    const LbSubsetEntryPtr& entry = vs_it->second;
    if (entry->host_subset_ != nullptr && i + 1 == matches.size()) {
      return entry->host_subset_;
    }

    subsets = &entry->children_;
  }

  return nullptr;
}

// Given the addition and/or removal of hosts, update all subsets to
// match, creating new subsets as necessary.
void SubsetLoadBalancer::update(const std::vector<HostSharedPtr>& hosts_added,
                                const std::vector<HostSharedPtr>& hosts_removed, bool local) {
  RELEASE_ASSERT(!local || original_local_host_set_);

  if (fallback_policy_ == envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT) {
    if (default_host_subset_ == nullptr) {
      // First update: create the default host subset with all hosts
      default_host_subset_ = newSubset([](const HostSharedPtr&) -> bool { return true; });
      default_host_subset_->lb_.reset(newLoadBalancer(default_host_subset_));
    } else {
      if (local) {
        default_host_subset_->local_host_set_.update(hosts_added, hosts_removed);
      } else {
        default_host_subset_->host_set_.update(hosts_added, hosts_removed);
      }
    }
  } else if (fallback_policy_ == envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET) {
    if (default_host_subset_ == nullptr) {
      // First update: create the default host subset
      default_host_subset_ = newSubset(
          std::bind(&SubsetLoadBalancer::hostMatchesDefaultSubset, this, std::placeholders::_1));
      default_host_subset_->lb_.reset(newLoadBalancer(default_host_subset_));
    } else {
      // Filter out added hosts that don't match the default subset
      std::vector<HostSharedPtr> subset_hosts_added;
      for (const auto& host : hosts_added) {
        if (hostMatchesDefaultSubset(host)) {
          subset_hosts_added.emplace_back(host);
        }
      }

      if (!subset_hosts_added.empty() || !hosts_removed.empty()) {
        if (local) {
          default_host_subset_->local_host_set_.update(subset_hosts_added, hosts_removed);
        } else {
          default_host_subset_->host_set_.update(subset_hosts_added, hosts_removed);
        }
      }
    }
  }

  std::unordered_set<SubsetPtr> subsets_created;
  std::unordered_map<SubsetPtr, std::vector<HostSharedPtr>> subset_hosts_added;
  std::unordered_map<SubsetPtr, std::vector<HostSharedPtr>> subset_hosts_removed;

  SubsetMetadata kvs;
  for (const auto& host : hosts_added) {
    for (const auto& keys : subset_keys_) {
      if (extractSubsetMetadata(keys, host, &kvs)) {
        LbSubsetEntryPtr entry = findOrCreateSubset(subsets_, kvs, 0, true);
        if (entry->host_subset_->lb_ == nullptr) {
          subsets_created.emplace(entry->host_subset_);
        } else {
          subset_hosts_added[entry->host_subset_].emplace_back(host);
        }
      }
    }
  }

  for (const auto& host : hosts_removed) {
    for (const auto& keys : subset_keys_) {
      if (extractSubsetMetadata(keys, host, &kvs)) {
        LbSubsetEntryPtr entry = findOrCreateSubset(subsets_, kvs, 0, false);
        if (entry == nullptr || entry->host_subset_->lb_ == nullptr) {
          // not found or brand new subset: ignore removals
          continue;
        }

        subset_hosts_removed[entry->host_subset_].emplace_back(host);
      }
    }
  }

  for (const auto& added : subset_hosts_added) {
    HostSubsetImpl& host_subset = local ? added.first->local_host_set_ : added.first->host_set_;

    const auto& removed_it = subset_hosts_removed.find(added.first);
    if (removed_it != subset_hosts_removed.end()) {
      host_subset.update(added.second, removed_it->second);
      subset_hosts_removed.erase(removed_it);
    } else {
      host_subset.update(added.second, {});
    }
  }

  for (const auto& removed : subset_hosts_removed) {
    const SubsetPtr& subset = removed.first;
    bool previously_empty = subset->empty();
    if (local) {
      subset->local_host_set_.update({}, removed.second);
    } else {
      subset->host_set_.update({}, removed.second);
    }

    if (subset->empty() && !previously_empty) {
      stats_.lb_subsets_active_.dec();
      stats_.lb_subsets_removed_.inc();
    }
  }

  for (const auto& subset : subsets_created) {
    subset->lb_.reset(newLoadBalancer(subset));
    if (!subset->empty()) {
      stats_.lb_subsets_active_.inc();
      stats_.lb_subsets_created_.inc();
    }
  }
}

bool SubsetLoadBalancer::hostMatchesDefaultSubset(const HostSharedPtr& host) {
  const envoy::api::v2::Metadata& host_metadata = host->metadata();

  for (const auto& it : default_subset_.fields()) {
    const ProtobufWkt::Value& host_value = Config::Metadata::metadataValue(
        host_metadata, Config::MetadataFilters::get().ENVOY_LB, it.first);

    if (!ValueUtil::equal(host_value, it.second)) {
      return false;
    }
  }

  return true;
}

bool SubsetLoadBalancer::hostMatches(const SubsetMetadata& kvs, const HostSharedPtr& host) {
  const envoy::api::v2::Metadata& host_metadata = host->metadata();

  for (const auto& kv : kvs) {
    const ProtobufWkt::Value& host_value = Config::Metadata::metadataValue(
        host_metadata, Config::MetadataFilters::get().ENVOY_LB, kv.first);

    if (!ValueUtil::equal(host_value, kv.second)) {
      return false;
    }
  }

  return true;
}

// Iterates over subset_keys looking up values from the given host's
// metadata. Each key-value pair is appended to kvs. Returns true if
// the host has a value for each key. The kvs vector is cleared before
// use.
bool SubsetLoadBalancer::extractSubsetMetadata(const std::set<std::string>& subset_keys,
                                               const HostSharedPtr& host, SubsetMetadata* kvs) {
  ASSERT(kvs != nullptr);
  kvs->clear();

  const envoy::api::v2::Metadata& metadata = host->metadata();
  const auto& filter_it = metadata.filter_metadata().find(Config::MetadataFilters::get().ENVOY_LB);
  if (filter_it == metadata.filter_metadata().end()) {
    return false;
  }

  const auto& fields = filter_it->second.fields();
  for (const auto key : subset_keys) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
      break;
    }
    kvs->emplace_back(std::pair<std::string, ProtobufWkt::Value>(key, it->second));
  }

  return kvs->size() == subset_keys.size();
}

// Given a vector of key-values (from extractSubsetMetadata),
// recursively finds the matching LbSubsetEntryPtr.  If it does not
// exist and allow_create is true, create the new subset.
SubsetLoadBalancer::LbSubsetEntryPtr
SubsetLoadBalancer::findOrCreateSubset(LbSubsetMap& subsets, const SubsetMetadata& kvs, size_t idx,
                                       bool allow_create) {
  const std::string& name = kvs[idx].first;
  const ProtobufWkt::Value& pb_value = kvs[idx].second;
  const HashedValue value(pb_value);

  const auto& kv_it = subsets.find(name);
  if (kv_it != subsets.end()) {
    ValueSubsetMap& value_subset_map = kv_it->second;
    const auto vs_it = value_subset_map.find(value);
    if (vs_it != value_subset_map.end()) {
      LbSubsetEntryPtr entry = vs_it->second;
      idx++;
      if (idx == kvs.size()) {
        if (entry->host_subset_ == nullptr) {
          // More specific nested subsets may exist, but this one does not.
          if (!allow_create) {
            return nullptr;
          }
          entry->host_subset_ = newSubset(
              std::bind(&SubsetLoadBalancer::hostMatches, this, kvs, std::placeholders::_1));
        }
        return entry;
      }

      return findOrCreateSubset(entry->children_, kvs, idx, allow_create);
    }
  }

  // not found, but don't want creation
  if (!allow_create) {
    return nullptr;
  }

  LbSubsetEntryPtr entry(new LbSubsetEntry());
  if (kv_it != subsets.end()) {
    ValueSubsetMap& value_subset_map = kv_it->second;
    value_subset_map.emplace(value, entry);
  } else {
    ValueSubsetMap value_subset_map = {{value, entry}};
    subsets.emplace(name, value_subset_map);
  }

  idx++;
  if (idx == kvs.size()) {
    entry->host_subset_ =
        newSubset(std::bind(&SubsetLoadBalancer::hostMatches, this, kvs, std::placeholders::_1));
    return entry;
  }

  return findOrCreateSubset(entry->children_, kvs, idx, true);
}

// Create a new Subset from the current HostSets, filtering hosts with
// the given predicate.
SubsetLoadBalancer::SubsetPtr
SubsetLoadBalancer::newSubset(std::function<bool(const HostSharedPtr&)> predicate) {
  std::vector<HostSharedPtr> hosts;
  for (const auto& host : original_host_set_.hosts()) {
    if (predicate(host)) {
      hosts.emplace_back(host);
    }
  }

  SubsetPtr subset;
  std::vector<HostSharedPtr> local_hosts;
  if (original_local_host_set_) {
    for (const auto& host : original_local_host_set_->hosts()) {
      if (predicate(host)) {
        local_hosts.emplace_back(host);
      }
    }

    subset.reset(new Subset(original_host_set_, *original_local_host_set_));
  } else {
    subset.reset(new Subset(original_host_set_, empty_host_set_));
  }

  subset->host_set_.update(hosts, {});
  subset->local_host_set_.update(local_hosts, {});
  return subset;
}

LoadBalancer* SubsetLoadBalancer::newLoadBalancer(const SubsetPtr& subset) {
  const HostSet* local_host_set = nullptr;
  if (original_local_host_set_ != nullptr) {
    local_host_set = &subset->local_host_set_;
  }

  LoadBalancer* lb = nullptr;
  switch (lb_type_) {
  case LoadBalancerType::LeastRequest:
    lb = new LeastRequestLoadBalancer(subset->host_set_, local_host_set, stats_, runtime_, random_);
    break;

  case LoadBalancerType::Random:
    lb = new RandomLoadBalancer(subset->host_set_, local_host_set, stats_, runtime_, random_);
    break;

  case LoadBalancerType::RoundRobin:
    lb = new RoundRobinLoadBalancer(subset->host_set_, local_host_set, stats_, runtime_, random_);
    break;

  case LoadBalancerType::RingHash:
    lb = new RingHashLoadBalancer(subset->host_set_, stats_, runtime_, random_);
    break;

  case LoadBalancerType::OriginalDst:
    NOT_REACHED;
  }

  subset->host_set_.triggerCallbacks();
  if (local_host_set) {
    subset->local_host_set_.triggerCallbacks();
  }
  return lb;
}

// Find the index in which the original HostSet's hostsPerLocality vector
// contains the given host.
int SubsetLoadBalancer::HostSubsetImpl::findLocalityIndex(const HostSharedPtr& host) {
  int idx = 0;
  for (const auto locality_hosts : original_host_set_.hostsPerLocality()) {
    for (const auto locality_host : locality_hosts) {
      if (locality_host == host) {
        return idx;
      }
    }
    idx++;
  }

  return -1;
}

// Given hosts_added and hosts_removed, update the underlying
// HostSet. The hosts_added Hosts must be filtered to match hosts that
// belong in this subset. The hosts_removed Hosts are ignored if they
// are not currently a member of this subset.
void SubsetLoadBalancer::HostSubsetImpl::update(const std::vector<HostSharedPtr>& hosts_added,
                                                const std::vector<HostSharedPtr>& hosts_removed) {
  for (const auto host : hosts_added) {
    int locality_index = findLocalityIndex(host);
    host_to_locality_.emplace(host, locality_index);
  }

  bool removed = false;
  for (const auto host : hosts_removed) {
    const auto host_it = host_to_locality_.find(host);
    if (host_it != host_to_locality_.end()) {
      host_to_locality_.erase(host_it);
      removed = true;
    }
  }

  if (hosts_added.empty() && !removed) {
    return;
  }

  HostVectorSharedPtr hosts(new std::vector<HostSharedPtr>());
  HostVectorSharedPtr healthy_hosts(new std::vector<HostSharedPtr>());
  HostListsSharedPtr hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>());
  HostListsSharedPtr healthy_hosts_per_locality(new std::vector<std::vector<HostSharedPtr>>());

  for (const auto host : this->hosts()) {
    const auto host_it = host_to_locality_.find(host);
    if (host_it == host_to_locality_.end()) {
      continue;
    }

    hosts->emplace_back(host);
  }

  hosts->insert(hosts->end(), hosts_added.begin(), hosts_added.end());

  for (const auto host : *hosts) {
    if (host->healthy()) {
      healthy_hosts->emplace_back(host);
    }

    const auto host_it = host_to_locality_.find(host);
    ASSERT(host_it != host_to_locality_.end());
    int index = host_it->second;
    if (index >= 0) {
      if (hosts_per_locality->size() <= static_cast<size_t>(index)) {
        hosts_per_locality->resize(index + 1);
        healthy_hosts_per_locality->resize(index + 1);
      }

      hosts_per_locality->at(index).emplace_back(host);
      if (host->healthy()) {
        healthy_hosts_per_locality->at(index).emplace_back(host);
      }
    }
  }

  empty_ = hosts->empty();

  HostSetImpl::updateHosts(hosts, healthy_hosts, hosts_per_locality, healthy_hosts_per_locality,
                           hosts_added, hosts_removed);
}

} // namespace Upstream
} // namespace Envoy
