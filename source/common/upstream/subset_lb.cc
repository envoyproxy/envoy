#include "source/common/upstream/subset_lb.h"

#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/runtime/runtime.h"

#include "source/common/common/assert.h"
#include "source/common/config/metadata.h"
#include "source/common/config/well_known_names.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/common/upstream/maglev_lb.h"
#include "source/common/upstream/ring_hash_lb.h"

#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Upstream {

using HostPredicate = std::function<bool(const Host&)>;

SubsetLoadBalancer::SubsetLoadBalancer(
    LoadBalancerType lb_type, PrioritySet& priority_set, const PrioritySet* local_priority_set,
    ClusterLbStats& stats, Stats::Scope& scope, Runtime::Loader& runtime,
    Random::RandomGenerator& random, const LoadBalancerSubsetInfo& subsets,
    const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>&
        lb_ring_hash_config,
    const absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig>& lb_maglev_config,
    const absl::optional<envoy::config::cluster::v3::Cluster::RoundRobinLbConfig>&
        round_robin_config,
    const absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>&
        least_request_config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
    TimeSource& time_source)
    : lb_type_(lb_type), lb_ring_hash_config_(lb_ring_hash_config),
      lb_maglev_config_(lb_maglev_config), round_robin_config_(round_robin_config),
      least_request_config_(least_request_config), common_config_(common_config), stats_(stats),
      scope_(scope), runtime_(runtime), random_(random), fallback_policy_(subsets.fallbackPolicy()),
      metadata_fallback_policy_(subsets.metadataFallbackPolicy()),
      default_subset_metadata_(subsets.defaultSubset().fields().begin(),
                               subsets.defaultSubset().fields().end()),
      subset_selectors_(subsets.subsetSelectors()), original_priority_set_(priority_set),
      original_local_priority_set_(local_priority_set),
      locality_weight_aware_(subsets.localityWeightAware()),
      scale_locality_weight_(subsets.scaleLocalityWeight()), list_as_any_(subsets.listAsAny()),
      time_source_(time_source) {
  ASSERT(subsets.isEnabled());

  if (fallback_policy_ != envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK) {
    if (fallback_policy_ == envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT) {
      ENVOY_LOG(debug, "subset lb: creating any-endpoint fallback load balancer");
      initSubsetAnyOnce();
      fallback_subset_ = subset_any_;
    } else {
      ENVOY_LOG(debug, "subset lb: creating fallback load balancer for {}",
                describeMetadata(default_subset_metadata_));
      initSubsetDefaultOnce();
      fallback_subset_ = subset_default_;
    }
  }

  if (subsets.panicModeAny()) {
    initSubsetAnyOnce();
    panic_mode_subset_ = subset_any_;
  }

  initSubsetSelectorMap();

  // Create filtered default subset (if necessary) and other subsets based on current hosts.
  refreshSubsets();

  // Configure future updates.
  original_priority_set_callback_handle_ = priority_set.addPriorityUpdateCb(
      [this](uint32_t priority, const HostVector&, const HostVector&) {
        refreshSubsets(priority);
        purgeEmptySubsets(subsets_);
      });
}

SubsetLoadBalancer::~SubsetLoadBalancer() {
  // Ensure gauges reflect correct values.
  forEachSubset(subsets_, [&](LbSubsetEntryPtr entry) {
    if (entry->active()) {
      stats_.lb_subsets_removed_.inc();
      stats_.lb_subsets_active_.dec();
    }
  });
}

void SubsetLoadBalancer::refreshSubsets() {
  for (auto& host_set : original_priority_set_.hostSetsPerPriority()) {
    update(host_set->priority(), host_set->hosts());
  }
}

void SubsetLoadBalancer::refreshSubsets(uint32_t priority) {
  const auto& host_sets = original_priority_set_.hostSetsPerPriority();
  ASSERT(priority < host_sets.size());
  update(priority, host_sets[priority]->hosts());
}

void SubsetLoadBalancer::initSubsetAnyOnce() {
  if (!subset_any_) {
    subset_any_ = std::make_shared<LbSubsetEntry>();
    subset_any_->lb_subset_ =
        std::make_unique<PriorityLbSubset>(*this, locality_weight_aware_, scale_locality_weight_);
  }
}

void SubsetLoadBalancer::initSubsetDefaultOnce() {
  if (!subset_default_) {
    subset_default_ = std::make_shared<LbSubsetEntry>();
    subset_default_->lb_subset_ =
        std::make_unique<PriorityLbSubset>(*this, locality_weight_aware_, scale_locality_weight_);
  }
}

void SubsetLoadBalancer::initSubsetSelectorMap() {
  selectors_ = std::make_shared<SubsetSelectorMap>();
  SubsetSelectorMapPtr selectors;
  for (const auto& subset_selector : subset_selectors_) {
    const auto& selector_keys = subset_selector->selectorKeys();
    const auto& selector_fallback_policy = subset_selector->fallbackPolicy();
    const auto& selector_fallback_keys_subset = subset_selector->fallbackKeysSubset();

    uint32_t pos = 0;
    selectors = selectors_;
    for (const auto& key : selector_keys) {
      const auto& selector_it = selectors->subset_keys_.find(key);
      pos++;
      if (selector_it == selectors->subset_keys_.end()) {
        selectors->subset_keys_.emplace(std::make_pair(key, std::make_shared<SubsetSelectorMap>()));
        const auto& child_selector = selectors->subset_keys_.find(key);
        // if this is last key for given selector, check if it has fallback specified
        if (pos == selector_keys.size()) {
          child_selector->second->fallback_params_.fallback_policy_ = selector_fallback_policy;
          child_selector->second->fallback_params_.fallback_keys_subset_ =
              &selector_fallback_keys_subset;
          initSelectorFallbackSubset(selector_fallback_policy);
        }
        selectors = child_selector->second;
      } else {
        selectors = selector_it->second;
      }
    }
    selectors = selectors_;
  }
}

void SubsetLoadBalancer::initSelectorFallbackSubset(
    const envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
        LbSubsetSelectorFallbackPolicy& fallback_policy) {
  if (fallback_policy ==
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::ANY_ENDPOINT &&
      subset_any_ == nullptr) {
    ENVOY_LOG(debug, "subset lb: creating any-endpoint fallback load balancer for selector");
    initSubsetAnyOnce();
  } else if (fallback_policy == envoy::config::cluster::v3::Cluster::LbSubsetConfig::
                                    LbSubsetSelector::DEFAULT_SUBSET &&
             subset_default_ == nullptr) {
    ENVOY_LOG(debug, "subset lb: creating default subset fallback load balancer for selector");
    initSubsetDefaultOnce();
  }
}

HostConstSharedPtr SubsetLoadBalancer::chooseHost(LoadBalancerContext* context) {
  if (metadata_fallback_policy_ !=
      envoy::config::cluster::v3::
          Cluster_LbSubsetConfig_LbSubsetMetadataFallbackPolicy_FALLBACK_LIST) {
    return chooseHostIteration(context);
  }
  const ProtobufWkt::Value* metadata_fallbacks = getMetadataFallbackList(context);
  if (metadata_fallbacks == nullptr) {
    return chooseHostIteration(context);
  }

  LoadBalancerContextWrapper context_no_metadata_fallback = removeMetadataFallbackList(context);
  return chooseHostWithMetadataFallbacks(&context_no_metadata_fallback,
                                         metadata_fallbacks->list_value().values());
}

HostConstSharedPtr
SubsetLoadBalancer::chooseHostWithMetadataFallbacks(LoadBalancerContext* context,
                                                    const MetadataFallbacks& metadata_fallbacks) {

  if (metadata_fallbacks.empty()) {
    return chooseHostIteration(context);
  }

  for (const auto& metadata_override : metadata_fallbacks) {
    LoadBalancerContextWrapper context_wrapper(context, metadata_override.struct_value());
    const auto host = chooseHostIteration(&context_wrapper);
    if (host) {
      return host;
    }
  }
  return nullptr;
}

// assumes context->metadataMatchCriteria() is not null and there is 'fallback_list' criterion
SubsetLoadBalancer::LoadBalancerContextWrapper
SubsetLoadBalancer::removeMetadataFallbackList(LoadBalancerContext* context) {
  ASSERT(context->metadataMatchCriteria());
  const auto& match_criteria = context->metadataMatchCriteria()->metadataMatchCriteria();

  std::set<std::string> to_preserve;
  for (const auto& criterion : match_criteria) {
    if (criterion->name() != Config::MetadataEnvoyLbKeys::get().FALLBACK_LIST) {
      to_preserve.emplace(criterion->name());
    }
  }
  return {context, to_preserve};
}

const ProtobufWkt::Value*
SubsetLoadBalancer::getMetadataFallbackList(LoadBalancerContext* context) const {
  if (context == nullptr) {
    return nullptr;
  }
  const auto& match_criteria = context->metadataMatchCriteria();
  if (match_criteria == nullptr) {
    return nullptr;
  }

  for (const auto& criterion : match_criteria->metadataMatchCriteria()) {
    if (criterion->name() == Config::MetadataEnvoyLbKeys::get().FALLBACK_LIST) {
      return &criterion->value().value();
    } // TODO(MarcinFalkowski): optimization: stop iteration when lexically after 'fallback_list'
  }
  return nullptr;
}

HostConstSharedPtr SubsetLoadBalancer::chooseHostIteration(LoadBalancerContext* context) {
  if (context) {
    bool host_chosen;
    HostConstSharedPtr host = tryChooseHostFromContext(context, host_chosen);
    if (host_chosen) {
      // Subset lookup succeeded, return this result even if it's nullptr.
      return host;
    }
    // otherwise check if there is fallback policy configured for given route metadata
    absl::optional<SubsetSelectorFallbackParamsRef> selector_fallback_params =
        tryFindSelectorFallbackParams(context);
    if (selector_fallback_params &&
        selector_fallback_params->get().fallback_policy_ !=
            envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED) {
      // return result according to configured fallback policy
      return chooseHostForSelectorFallbackPolicy(*selector_fallback_params, context);
    }
  }

  if (fallback_subset_ == nullptr) {
    return nullptr;
  }

  HostConstSharedPtr host = fallback_subset_->lb_subset_->chooseHost(context);
  if (host != nullptr) {
    stats_.lb_subsets_fallback_.inc();
    return host;
  }

  if (panic_mode_subset_ != nullptr) {
    HostConstSharedPtr host = panic_mode_subset_->lb_subset_->chooseHost(context);
    if (host != nullptr) {
      stats_.lb_subsets_fallback_panic_.inc();
      return host;
    }
  }

  return nullptr;
}

absl::optional<SubsetLoadBalancer::SubsetSelectorFallbackParamsRef>
SubsetLoadBalancer::tryFindSelectorFallbackParams(LoadBalancerContext* context) {
  const Router::MetadataMatchCriteria* match_criteria = context->metadataMatchCriteria();
  if (!match_criteria) {
    return absl::nullopt;
  }
  const auto match_criteria_vec = match_criteria->metadataMatchCriteria();
  SubsetSelectorMapPtr selectors = selectors_;
  if (selectors == nullptr) {
    return absl::nullopt;
  }
  for (uint32_t i = 0; i < match_criteria_vec.size(); i++) {
    const Router::MetadataMatchCriterion& match_criterion = *match_criteria_vec[i];
    const auto& subset_it = selectors->subset_keys_.find(match_criterion.name());
    if (subset_it == selectors->subset_keys_.end()) {
      // No subsets with this key (at this level in the hierarchy).
      break;
    }

    if (i + 1 == match_criteria_vec.size()) {
      // We've reached the end of the criteria, and they all matched.
      return subset_it->second->fallback_params_;
    }
    selectors = subset_it->second;
  }

  return absl::nullopt;
}

HostConstSharedPtr SubsetLoadBalancer::chooseHostForSelectorFallbackPolicy(
    const SubsetSelectorFallbackParams& fallback_params, LoadBalancerContext* context) {
  const auto& fallback_policy = fallback_params.fallback_policy_;
  if (fallback_policy ==
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::ANY_ENDPOINT &&
      subset_any_ != nullptr) {
    return subset_any_->lb_subset_->chooseHost(context);
  } else if (fallback_policy == envoy::config::cluster::v3::Cluster::LbSubsetConfig::
                                    LbSubsetSelector::DEFAULT_SUBSET &&
             subset_default_ != nullptr) {
    return subset_default_->lb_subset_->chooseHost(context);
  } else if (fallback_policy ==
             envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET) {
    ASSERT(fallback_params.fallback_keys_subset_);
    auto filtered_context = std::make_unique<LoadBalancerContextWrapper>(
        context, *fallback_params.fallback_keys_subset_);
    // Perform whole subset load balancing again with reduced metadata match criteria
    return chooseHostIteration(filtered_context.get());
  } else {
    return nullptr;
  }
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
  return entry->lb_subset_->chooseHost(context);
}

// Iterates over the given metadata match criteria (which must be lexically sorted by key) and find
// a matching LbSubsetEntryPtr, if any.
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

void SubsetLoadBalancer::updateFallbackSubset(uint32_t priority, const HostVector& all_hosts) {
  auto update_func = [priority, &all_hosts](LbSubsetPtr& subset, const HostPredicate& predicate) {
    for (const auto& host : all_hosts) {
      if (predicate(*host)) {
        subset->pushHost(priority, host);
      }
    }
    subset->finalize(priority);
  };

  if (subset_any_ != nullptr) {
    update_func(subset_any_->lb_subset_, [](const Host&) { return true; });
  }

  if (subset_default_ != nullptr) {
    HostPredicate predicate = std::bind(&SubsetLoadBalancer::hostMatches, this,
                                        default_subset_metadata_, std::placeholders::_1);
    update_func(subset_default_->lb_subset_, predicate);
  }

  if (fallback_subset_ == nullptr) {
    ENVOY_LOG(debug, "subset lb: fallback load balancer disabled");
    return;
  }

  // Same thing for the panic mode subset.
  ASSERT(panic_mode_subset_ == nullptr || panic_mode_subset_ == subset_any_);
}

void SubsetLoadBalancer::initLbSubsetEntryOnce(LbSubsetEntryPtr& entry, bool single_host_subset) {
  ASSERT(entry != nullptr);
  if (entry->initialized()) {
    return;
  }

  if (single_host_subset) {
    entry->lb_subset_ = std::make_unique<SingleHostLbSubset>();
    entry->single_host_subset_ = true;
  } else {
    entry->lb_subset_ =
        std::make_unique<PriorityLbSubset>(*this, locality_weight_aware_, scale_locality_weight_);
    entry->single_host_subset_ = false;
  }

  stats_.lb_subsets_active_.inc();
  stats_.lb_subsets_created_.inc();
}

// Iterates all the hosts of specified priority, looking up an LbSubsetEntryPtr for each and add
// hosts to related entry. Because the metadata of host can be updated inlined, we must evaluate
// every hosts for every update.
void SubsetLoadBalancer::processSubsets(uint32_t priority, const HostVector& all_hosts) {
  absl::flat_hash_set<const LbSubsetEntry*> single_host_entries;
  uint64_t collision_count_of_single_host_entries{};

  for (const auto& host : all_hosts) {
    for (const auto& subset_selector : subset_selectors_) {
      const auto& keys = subset_selector->selectorKeys();
      // For each host, for each subset key, attempt to extract the metadata corresponding to the
      // key from the host.
      std::vector<SubsetMetadata> all_kvs = extractSubsetMetadata(keys, *host);
      for (const auto& kvs : all_kvs) {
        // The host has metadata for each key, find or create its subset.
        auto entry = findOrCreateLbSubsetEntry(subsets_, kvs, 0);
        initLbSubsetEntryOnce(entry, subset_selector->singleHostPerSubset());

        if (entry->single_host_subset_) {
          if (single_host_entries.contains(entry.get())) {
            collision_count_of_single_host_entries++;
            continue;
          }
          single_host_entries.emplace(entry.get());
        }

        entry->lb_subset_->pushHost(priority, host);
      }
    }
  }

  // This stat isn't added to `ClusterTrafficStats` because it wouldn't be used for nearly all
  // clusters, and is only set during configuration updates, not in the data path, so performance of
  // looking up the stat isn't critical.
  if (single_duplicate_stat_ == nullptr) {
    Stats::StatNameManagedStorage name_storage("lb_subsets_single_host_per_subset_duplicate",
                                               scope_.symbolTable());

    single_duplicate_stat_ = &Stats::Utility::gaugeFromElements(
        scope_, {name_storage.statName()}, Stats::Gauge::ImportMode::Accumulate);
  }
  single_duplicate_stat_->set(collision_count_of_single_host_entries);

  // Finalize updates after all the hosts are evaluated.
  forEachSubset(subsets_, [priority](LbSubsetEntryPtr entry) {
    if (entry->initialized()) {
      entry->lb_subset_->finalize(priority);
    }
  });
}

// Given the latest all hosts, update all subsets for this priority level, creating new subsets as
// necessary.
void SubsetLoadBalancer::update(uint32_t priority, const HostVector& all_hosts) {
  updateFallbackSubset(priority, all_hosts);
  processSubsets(priority, all_hosts);
}

bool SubsetLoadBalancer::hostMatches(const SubsetMetadata& kvs, const Host& host) {
  return Config::Metadata::metadataLabelMatch(
      kvs, host.metadata().get(), Config::MetadataFilters::get().ENVOY_LB, list_as_any_);
}

// Iterates over subset_keys looking up values from the given host's metadata. Each key-value pair
// is appended to kvs. Returns a non-empty value if the host has a value for each key.
std::vector<SubsetLoadBalancer::SubsetMetadata>
SubsetLoadBalancer::extractSubsetMetadata(const std::set<std::string>& subset_keys,
                                          const Host& host) {
  std::vector<SubsetMetadata> all_kvs;
  if (!host.metadata()) {
    return all_kvs;
  }
  const envoy::config::core::v3::Metadata& metadata = *host.metadata();
  const auto& filter_it = metadata.filter_metadata().find(Config::MetadataFilters::get().ENVOY_LB);
  if (filter_it == metadata.filter_metadata().end()) {
    return all_kvs;
  }

  const auto& fields = filter_it->second.fields();
  for (const auto& key : subset_keys) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
      all_kvs.clear();
      break;
    }

    if (list_as_any_ && it->second.kind_case() == ProtobufWkt::Value::kListValue) {
      // If the list of kvs is empty, we initialize one kvs for each value in the list.
      // Otherwise, we branch the list of kvs by generating one new kvs per old kvs per
      // new value.
      //
      // For example, two kvs (<a=1>, <a=2>) joined with the kv foo=[bar,baz] results in four kvs:
      //   <a=1,foo=bar>
      //   <a=1,foo=baz>
      //   <a=2,foo=bar>
      //   <a=2,foo=baz>
      if (all_kvs.empty()) {
        for (const auto& v : it->second.list_value().values()) {
          all_kvs.emplace_back(SubsetMetadata({make_pair(key, v)}));
        }
      } else {
        std::vector<SubsetMetadata> new_kvs;
        for (const auto& kvs : all_kvs) {
          for (const auto& v : it->second.list_value().values()) {
            auto kv_copy = kvs;
            kv_copy.emplace_back(make_pair(key, v));
            new_kvs.emplace_back(kv_copy);
          }
        }
        all_kvs = new_kvs;
      }

    } else {
      if (all_kvs.empty()) {
        all_kvs.emplace_back(SubsetMetadata({std::make_pair(key, it->second)}));
      } else {
        for (auto& kvs : all_kvs) {
          kvs.emplace_back(std::make_pair(key, it->second));
        }
      }
    }
  }

  return all_kvs;
}

std::string SubsetLoadBalancer::describeMetadata(const SubsetLoadBalancer::SubsetMetadata& kvs) {
  if (kvs.empty()) {
    return "<no metadata>";
  }

  std::ostringstream buf;
  bool first = true;
  for (const auto& it : kvs) {
    if (!first) {
      buf << ", ";
    } else {
      first = false;
    }

    const ProtobufWkt::Value& value = it.second;
    buf << it.first << "=" << MessageUtil::getJsonStringFromMessageOrDie(value);
  }

  return buf.str();
}

// Given a vector of key-values (from extractSubsetMetadata), recursively finds the matching
// LbSubsetEntryPtr.
SubsetLoadBalancer::LbSubsetEntryPtr
SubsetLoadBalancer::findOrCreateLbSubsetEntry(LbSubsetMap& subsets, const SubsetMetadata& kvs,
                                              uint32_t idx) {
  ASSERT(idx < kvs.size());

  const std::string& name = kvs[idx].first;
  const ProtobufWkt::Value& pb_value = kvs[idx].second;
  const HashedValue value(pb_value);

  LbSubsetEntryPtr entry;

  const auto kv_it = subsets.find(name);

  if (kv_it != subsets.end()) {
    ValueSubsetMap& value_subset_map = kv_it->second;
    const auto vs_it = value_subset_map.find(value);
    if (vs_it != value_subset_map.end()) {
      entry = vs_it->second;
    }
  }

  if (!entry) {
    // Not found. Create an uninitialized entry.
    entry = std::make_shared<LbSubsetEntry>();
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

  return findOrCreateLbSubsetEntry(entry->children_, kvs, idx);
}

// Invokes cb for each LbSubsetEntryPtr in subsets.
void SubsetLoadBalancer::forEachSubset(LbSubsetMap& subsets,
                                       std::function<void(LbSubsetEntryPtr&)> cb) {
  for (auto& vsm : subsets) {
    for (auto& em : vsm.second) {
      LbSubsetEntryPtr entry = em.second;
      cb(entry);
      forEachSubset(entry->children_, cb);
    }
  }
}

void SubsetLoadBalancer::purgeEmptySubsets(LbSubsetMap& subsets) {
  for (auto subset_it = subsets.begin(); subset_it != subsets.end();) {
    for (auto it = subset_it->second.begin(); it != subset_it->second.end();) {
      LbSubsetEntryPtr entry = it->second;

      purgeEmptySubsets(entry->children_);

      if (entry->active() || entry->hasChildren()) {
        it++;
        continue;
      }

      // If it wasn't initialized, it wasn't accounted for.
      if (entry->initialized()) {
        stats_.lb_subsets_active_.dec();
        stats_.lb_subsets_removed_.inc();
      }

      auto next_it = std::next(it);
      subset_it->second.erase(it);
      it = next_it;
    }

    if (subset_it->second.empty()) {
      auto next_subset_it = std::next(subset_it);
      subsets.erase(subset_it);
      subset_it = next_subset_it;
    } else {
      subset_it++;
    }
  }
}

// Initialize a new HostSubsetImpl and LoadBalancer from the SubsetLoadBalancer, filtering hosts
// with the given predicate.
SubsetLoadBalancer::PrioritySubsetImpl::PrioritySubsetImpl(const SubsetLoadBalancer& subset_lb,
                                                           bool locality_weight_aware,
                                                           bool scale_locality_weight)
    : original_priority_set_(subset_lb.original_priority_set_),
      locality_weight_aware_(locality_weight_aware), scale_locality_weight_(scale_locality_weight) {
  // Create at least one host set.
  getOrCreateHostSet(0);

  switch (subset_lb.lb_type_) {
  case LoadBalancerType::LeastRequest:
    lb_ = std::make_unique<LeastRequestLoadBalancer>(
        *this, subset_lb.original_local_priority_set_, subset_lb.stats_, subset_lb.runtime_,
        subset_lb.random_, subset_lb.common_config_, subset_lb.least_request_config_,
        subset_lb.time_source_);
    break;

  case LoadBalancerType::Random:
    lb_ = std::make_unique<RandomLoadBalancer>(*this, subset_lb.original_local_priority_set_,
                                               subset_lb.stats_, subset_lb.runtime_,
                                               subset_lb.random_, subset_lb.common_config_);
    break;

  case LoadBalancerType::RoundRobin:
    lb_ = std::make_unique<RoundRobinLoadBalancer>(
        *this, subset_lb.original_local_priority_set_, subset_lb.stats_, subset_lb.runtime_,
        subset_lb.random_, subset_lb.common_config_, subset_lb.round_robin_config_,
        subset_lb.time_source_);
    break;

  case LoadBalancerType::RingHash:
    // TODO(mattklein123): The ring hash LB is thread aware, but currently the subset LB is not.
    // We should make the subset LB thread aware since the calculations are costly, and then we
    // can also use a thread aware sub-LB properly. The following works fine but is not optimal.
    thread_aware_lb_ = std::make_unique<RingHashLoadBalancer>(
        *this, subset_lb.stats_, subset_lb.scope_, subset_lb.runtime_, subset_lb.random_,
        subset_lb.lb_ring_hash_config_, subset_lb.common_config_);
    thread_aware_lb_->initialize();
    lb_ = thread_aware_lb_->factory()->create();
    break;

  case LoadBalancerType::Maglev:
    // TODO(mattklein123): The Maglev LB is thread aware, but currently the subset LB is not.
    // We should make the subset LB thread aware since the calculations are costly, and then we
    // can also use a thread aware sub-LB properly. The following works fine but is not optimal.
    thread_aware_lb_ = std::make_unique<MaglevLoadBalancer>(
        *this, subset_lb.stats_, subset_lb.scope_, subset_lb.runtime_, subset_lb.random_,
        subset_lb.lb_maglev_config_, subset_lb.common_config_);
    thread_aware_lb_->initialize();
    lb_ = thread_aware_lb_->factory()->create();
    break;

  case LoadBalancerType::OriginalDst:
  case LoadBalancerType::ClusterProvided:
  case LoadBalancerType::LoadBalancingPolicyConfig:
    // These load balancer types can only be created when there is no subset configuration.
    PANIC("not implemented");
  }

  triggerCallbacks();
}

// Given all hosts that that belong in this subset, hosts_added and hosts_removed, update the
// underlying HostSet. The hosts_added Hosts and hosts_removed Hosts have been filtered to match
// hosts that belong in this subset.
void SubsetLoadBalancer::HostSubsetImpl::update(const HostHashSet& matching_hosts,
                                                const HostVector& hosts_added,
                                                const HostVector& hosts_removed) {
  auto cached_predicate = [&matching_hosts](const auto& host) {
    return matching_hosts.count(&host) == 1;
  };

  // TODO(snowp): If we had a unhealthyHosts() function we could avoid potentially traversing
  // the list of hosts twice.
  auto hosts = std::make_shared<HostVector>();
  hosts->reserve(original_host_set_.hosts().size());
  for (const auto& host : original_host_set_.hosts()) {
    if (cached_predicate(*host)) {
      hosts->emplace_back(host);
    }
  }

  auto healthy_hosts = std::make_shared<HealthyHostVector>();
  healthy_hosts->get().reserve(original_host_set_.healthyHosts().size());
  for (const auto& host : original_host_set_.healthyHosts()) {
    if (cached_predicate(*host)) {
      healthy_hosts->get().emplace_back(host);
    }
  }

  auto degraded_hosts = std::make_shared<DegradedHostVector>();
  degraded_hosts->get().reserve(original_host_set_.degradedHosts().size());
  for (const auto& host : original_host_set_.degradedHosts()) {
    if (cached_predicate(*host)) {
      degraded_hosts->get().emplace_back(host);
    }
  }

  auto excluded_hosts = std::make_shared<ExcludedHostVector>();
  excluded_hosts->get().reserve(original_host_set_.excludedHosts().size());
  for (const auto& host : original_host_set_.excludedHosts()) {
    if (cached_predicate(*host)) {
      excluded_hosts->get().emplace_back(host);
    }
  }

  // If we only have one locality we can avoid the first call to filter() by
  // just creating a new HostsPerLocality from the list of all hosts.
  HostsPerLocalityConstSharedPtr hosts_per_locality;

  if (original_host_set_.hostsPerLocality().get().size() == 1) {
    hosts_per_locality = std::make_shared<HostsPerLocalityImpl>(
        *hosts, original_host_set_.hostsPerLocality().hasLocalLocality());
  } else {
    hosts_per_locality = original_host_set_.hostsPerLocality().filter({cached_predicate})[0];
  }

  auto healthy_hosts_per_locality =
      original_host_set_.healthyHostsPerLocality().filter({cached_predicate})[0];
  auto degraded_hosts_per_locality =
      original_host_set_.degradedHostsPerLocality().filter({cached_predicate})[0];
  auto excluded_hosts_per_locality =
      original_host_set_.excludedHostsPerLocality().filter({cached_predicate})[0];

  HostSetImpl::updateHosts(
      HostSetImpl::updateHostsParams(
          hosts, hosts_per_locality, healthy_hosts, healthy_hosts_per_locality, degraded_hosts,
          degraded_hosts_per_locality, excluded_hosts, excluded_hosts_per_locality),
      determineLocalityWeights(*hosts_per_locality), hosts_added, hosts_removed, absl::nullopt);
}

LocalityWeightsConstSharedPtr SubsetLoadBalancer::HostSubsetImpl::determineLocalityWeights(
    const HostsPerLocality& hosts_per_locality) const {
  if (locality_weight_aware_) {
    if (scale_locality_weight_) {
      const auto& original_hosts_per_locality = original_host_set_.hostsPerLocality().get();
      // E.g. we can be here in static clusters with actual locality weighting before pre-init
      // completes.
      if (!original_host_set_.localityWeights()) {
        return {};
      }
      const auto& original_weights = *original_host_set_.localityWeights();

      auto scaled_locality_weights = std::make_shared<LocalityWeights>(original_weights.size());
      for (uint32_t i = 0; i < original_weights.size(); ++i) {
        // If the original locality has zero hosts, skip it. This leaves the weight at zero.
        if (original_hosts_per_locality[i].empty()) {
          continue;
        }

        // Otherwise, scale it proportionally to the number of hosts removed by the subset
        // predicate.
        (*scaled_locality_weights)[i] =
            std::round(float((original_weights[i] * hosts_per_locality.get()[i].size())) /
                       original_hosts_per_locality[i].size());
      }

      return scaled_locality_weights;
    } else {
      return original_host_set_.localityWeights();
    }
  }
  return {};
}

HostSetImplPtr SubsetLoadBalancer::PrioritySubsetImpl::createHostSet(
    uint32_t priority, absl::optional<uint32_t> overprovisioning_factor) {
  // Use original hostset's overprovisioning_factor.
  RELEASE_ASSERT(priority < original_priority_set_.hostSetsPerPriority().size(), "");

  const HostSetPtr& host_set = original_priority_set_.hostSetsPerPriority()[priority];

  ASSERT(!overprovisioning_factor.has_value() ||
         overprovisioning_factor.value() == host_set->overprovisioningFactor());
  return HostSetImplPtr{
      new HostSubsetImpl(*host_set, locality_weight_aware_, scale_locality_weight_)};
}

void SubsetLoadBalancer::PrioritySubsetImpl::update(uint32_t priority,
                                                    const HostHashSet& matching_hosts,
                                                    const HostVector& hosts_added,
                                                    const HostVector& hosts_removed) {
  const auto& host_subset = getOrCreateHostSet(priority);
  updateSubset(priority, matching_hosts, hosts_added, hosts_removed);

  if (host_subset.hosts().empty() != empty_) {
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

SubsetLoadBalancer::LoadBalancerContextWrapper::LoadBalancerContextWrapper(
    LoadBalancerContext* wrapped,
    const std::set<std::string>& filtered_metadata_match_criteria_names)
    : wrapped_(wrapped) {
  ASSERT(wrapped->metadataMatchCriteria());

  metadata_match_ =
      wrapped->metadataMatchCriteria()->filterMatchCriteria(filtered_metadata_match_criteria_names);
}

SubsetLoadBalancer::LoadBalancerContextWrapper::LoadBalancerContextWrapper(
    LoadBalancerContext* wrapped, const ProtobufWkt::Struct& metadata_match_criteria_override)
    : wrapped_(wrapped) {
  ASSERT(wrapped->metadataMatchCriteria());
  metadata_match_ =
      wrapped->metadataMatchCriteria()->mergeMatchCriteria(metadata_match_criteria_override);
}
} // namespace Upstream
} // namespace Envoy
