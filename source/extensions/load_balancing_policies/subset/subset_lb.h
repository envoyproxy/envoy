#pragma once

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/load_balancing_policies/subset/v3/subset.pb.h"
#include "envoy/extensions/load_balancing_policies/subset/v3/subset.pb.validate.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/macros.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/common/upstream/upstream_impl.h"

#include "absl/container/node_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {

class ChildLoadBalancerCreator {
public:
  virtual ~ChildLoadBalancerCreator() = default;

  virtual std::pair<ThreadAwareLoadBalancerPtr, LoadBalancerPtr>
  createLoadBalancer(const PrioritySet& child_priority_set, const PrioritySet* local_priority_set,
                     ClusterLbStats& stats, Stats::Scope& scope, Runtime::Loader& runtime,
                     Random::RandomGenerator& random, TimeSource& time_source) PURE;
};
using ChildLoadBalancerCreatorPtr = std::unique_ptr<ChildLoadBalancerCreator>;

class LegacyChildLoadBalancerCreatorImpl : public Upstream::ChildLoadBalancerCreator {
public:
  LegacyChildLoadBalancerCreatorImpl(
      LoadBalancerType lb_type,
      OptRef<const envoy::config::cluster::v3::Cluster::RingHashLbConfig> lb_ring_hash_config,
      OptRef<const envoy::config::cluster::v3::Cluster::MaglevLbConfig> lb_maglev_config,
      OptRef<const envoy::config::cluster::v3::Cluster::RoundRobinLbConfig> round_robin_config,
      OptRef<const envoy::config::cluster::v3::Cluster::LeastRequestLbConfig> least_request_config,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);

  std::pair<Upstream::ThreadAwareLoadBalancerPtr, Upstream::LoadBalancerPtr>
  createLoadBalancer(const Upstream::PrioritySet& child_priority_set,
                     const Upstream::PrioritySet* local_priority_set, ClusterLbStats& stats,
                     Stats::Scope& scope, Runtime::Loader& runtime, Random::RandomGenerator& random,
                     TimeSource& time_source) override;

  OptRef<const envoy::config::cluster::v3::Cluster::RoundRobinLbConfig> lbRoundRobinConfig() const {
    if (round_robin_config_ != nullptr) {
      return *round_robin_config_;
    }
    return absl::nullopt;
  }

  LoadBalancerType lbType() const { return lb_type_; }

  OptRef<const envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>
  lbLeastRequestConfig() const {
    if (least_request_config_ != nullptr) {
      return *least_request_config_;
    }
    return absl::nullopt;
  }

  OptRef<const envoy::config::cluster::v3::Cluster::MaglevLbConfig> lbMaglevConfig() const {
    if (lb_maglev_config_ != nullptr) {
      return *lb_maglev_config_;
    }
    return absl::nullopt;
  }

  OptRef<const envoy::config::cluster::v3::Cluster::RingHashLbConfig> lbRingHashConfig() const {
    if (lb_ring_hash_config_ != nullptr) {
      return *lb_ring_hash_config_;
    }
    return absl::nullopt;
  }

private:
  const LoadBalancerType lb_type_;
  const std::unique_ptr<const envoy::config::cluster::v3::Cluster::RingHashLbConfig>
      lb_ring_hash_config_;
  const std::unique_ptr<const envoy::config::cluster::v3::Cluster::MaglevLbConfig>
      lb_maglev_config_;
  const std::unique_ptr<const envoy::config::cluster::v3::Cluster::RoundRobinLbConfig>
      round_robin_config_;
  const std::unique_ptr<const envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>
      least_request_config_;
  const envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
};

using HostHashSet = absl::flat_hash_set<HostSharedPtr>;

class SubsetLoadBalancerConfig : public Upstream::LoadBalancerConfig {
public:
  SubsetLoadBalancerConfig(const SubsetLoadbalancingPolicyProto& subset_config,
                           ProtobufMessage::ValidationVisitor& visitor);

  SubsetLoadBalancerConfig(const LegacySubsetLoadbalancingPolicyProto& subset_config,
                           Upstream::LoadBalancerConfigPtr sub_load_balancer_config,
                           Upstream::TypedLoadBalancerFactory* sub_load_balancer_factory)
      : subset_info_(subset_config), sub_load_balancer_config_(std::move(sub_load_balancer_config)),
        sub_load_balancer_factory_(sub_load_balancer_factory) {
    ASSERT(sub_load_balancer_factory_ != nullptr, "sub_load_balancer_factory_ must not be nullptr");
  }

  Upstream::ThreadAwareLoadBalancerPtr
  createLoadBalancer(const Upstream::ClusterInfo& cluster_info,
                     const Upstream::PrioritySet& child_priority_set, Runtime::Loader& runtime,
                     Random::RandomGenerator& random, TimeSource& time_source) const {
    return sub_load_balancer_factory_->create(*sub_load_balancer_config_, cluster_info,
                                              child_priority_set, runtime, random, time_source);
  }

  const Upstream::LoadBalancerSubsetInfo& subsetInfo() const { return subset_info_; }

private:
  LoadBalancerSubsetInfoImpl subset_info_;

  Upstream::LoadBalancerConfigPtr sub_load_balancer_config_;
  Upstream::TypedLoadBalancerFactory* sub_load_balancer_factory_{};
};

class SubsetLoadBalancer : public LoadBalancer, Logger::Loggable<Logger::Id::upstream> {
public:
  SubsetLoadBalancer(const LoadBalancerSubsetInfo& subsets, ChildLoadBalancerCreatorPtr child_lb,
                     const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                     ClusterLbStats& stats, Stats::Scope& scope, Runtime::Loader& runtime,
                     Random::RandomGenerator& random, TimeSource& time_source);
  ~SubsetLoadBalancer() override;

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;
  // TODO(alyssawilk) implement for non-metadata match.
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { return nullptr; }
  // Pool selection not implemented.
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                           const Upstream::Host& /*host*/,
                           std::vector<uint8_t>& /*hash_key*/) override {
    return absl::nullopt;
  }
  // Lifetime tracking not implemented.
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return {};
  }

private:
  struct SubsetSelectorFallbackParams;

  void initSubsetAnyOnce();
  void initSubsetDefaultOnce();
  void initSubsetSelectorMap();
  void initSelectorFallbackSubset(const envoy::config::cluster::v3::Cluster::LbSubsetConfig::
                                      LbSubsetSelector::LbSubsetSelectorFallbackPolicy&);
  HostConstSharedPtr
  chooseHostForSelectorFallbackPolicy(const SubsetSelectorFallbackParams& fallback_params,
                                      LoadBalancerContext* context);

  HostConstSharedPtr chooseHostIteration(LoadBalancerContext* context);

  // Represents a subset of an original HostSet.
  class HostSubsetImpl : public HostSetImpl {
  public:
    HostSubsetImpl(const HostSet& original_host_set, bool locality_weight_aware,
                   bool scale_locality_weight)
        : HostSetImpl(original_host_set.priority(), original_host_set.weightedPriorityHealth(),
                      original_host_set.overprovisioningFactor()),
          original_host_set_(original_host_set), locality_weight_aware_(locality_weight_aware),
          scale_locality_weight_(scale_locality_weight) {}

    void update(const HostHashSet& matching_hosts, const HostVector& hosts_added,
                const HostVector& hosts_removed);
    LocalityWeightsConstSharedPtr
    determineLocalityWeights(const HostsPerLocality& hosts_per_locality) const;

  private:
    const HostSet& original_host_set_;
    const bool locality_weight_aware_;
    const bool scale_locality_weight_;
  };

  // Represents a subset of an original PrioritySet.
  class PrioritySubsetImpl : public PrioritySetImpl {
  public:
    PrioritySubsetImpl(const SubsetLoadBalancer& subset_lb, bool locality_weight_aware,
                       bool scale_locality_weight);

    void update(uint32_t priority, const HostHashSet& matching_hosts, const HostVector& hosts_added,
                const HostVector& hosts_removed);

    bool empty() const { return empty_; }

    void triggerCallbacks() {
      for (size_t i = 0; i < hostSetsPerPriority().size(); ++i) {
        runReferenceUpdateCallbacks(i, {}, {});
      }
    }

    void updateSubset(uint32_t priority, const HostHashSet& matching_hosts,
                      const HostVector& hosts_added, const HostVector& hosts_removed) {
      reinterpret_cast<HostSubsetImpl*>(host_sets_[priority].get())
          ->update(matching_hosts, hosts_added, hosts_removed);
      runUpdateCallbacks(hosts_added, hosts_removed);
    }

    // Thread aware LB if applicable.
    ThreadAwareLoadBalancerPtr thread_aware_lb_;
    // Current active LB.
    LoadBalancerPtr lb_;

  protected:
    HostSetImplPtr createHostSet(uint32_t priority, absl::optional<bool> weighted_priority_health,
                                 absl::optional<uint32_t> overprovisioning_factor) override;

  private:
    const PrioritySet& original_priority_set_;
    const PrioritySet* original_local_priority_set_{};
    const bool locality_weight_aware_;
    const bool scale_locality_weight_;
    bool empty_ = true;
  };

  using HostSubsetImplPtr = std::unique_ptr<HostSubsetImpl>;
  using PrioritySubsetImplPtr = std::unique_ptr<PrioritySubsetImpl>;

  using SubsetMetadata = std::vector<std::pair<std::string, ProtobufWkt::Value>>;

  class LbSubsetEntry;
  struct SubsetSelectorMap;

  using LbSubsetEntryPtr = std::shared_ptr<LbSubsetEntry>;
  using SubsetSelectorMapPtr = std::shared_ptr<SubsetSelectorMap>;
  using ValueSubsetMap = absl::node_hash_map<HashedValue, LbSubsetEntryPtr>;
  using LbSubsetMap = absl::node_hash_map<std::string, ValueSubsetMap>;
  using SubsetSelectorFallbackParamsRef = std::reference_wrapper<SubsetSelectorFallbackParams>;
  using MetadataFallbacks = ProtobufWkt::RepeatedPtrField<ProtobufWkt::Value>;

public:
  class LoadBalancerContextWrapper : public LoadBalancerContext {
  public:
    LoadBalancerContextWrapper(LoadBalancerContext* wrapped,
                               const std::set<std::string>& filtered_metadata_match_criteria_names);

    LoadBalancerContextWrapper(LoadBalancerContext* wrapped,
                               Router::MetadataMatchCriteriaConstPtr metadata_match_criteria)
        : wrapped_(wrapped), metadata_match_(std::move(metadata_match_criteria)) {}

    LoadBalancerContextWrapper(LoadBalancerContext* wrapped,
                               const ProtobufWkt::Struct& metadata_match_criteria_override);
    // LoadBalancerContext
    absl::optional<uint64_t> computeHashKey() override { return wrapped_->computeHashKey(); }
    const Router::MetadataMatchCriteria* metadataMatchCriteria() override {
      return metadata_match_.get();
    }
    const Network::Connection* downstreamConnection() const override {
      return wrapped_->downstreamConnection();
    }
    const StreamInfo::StreamInfo* requestStreamInfo() const override {
      return wrapped_->requestStreamInfo();
    }
    const Http::RequestHeaderMap* downstreamHeaders() const override {
      return wrapped_->downstreamHeaders();
    }
    const HealthyAndDegradedLoad& determinePriorityLoad(
        const PrioritySet& priority_set, const HealthyAndDegradedLoad& original_priority_load,
        const Upstream::RetryPriority::PriorityMappingFunc& priority_mapping_func) override {
      return wrapped_->determinePriorityLoad(priority_set, original_priority_load,
                                             priority_mapping_func);
    }
    bool shouldSelectAnotherHost(const Host& host) override {
      return wrapped_->shouldSelectAnotherHost(host);
    }
    uint32_t hostSelectionRetryCount() const override {
      return wrapped_->hostSelectionRetryCount();
    }
    Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
      return wrapped_->upstreamSocketOptions();
    }
    Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
      return wrapped_->upstreamTransportSocketOptions();
    }

    absl::optional<OverrideHost> overrideHostToSelect() const override {
      return wrapped_->overrideHostToSelect();
    }

  private:
    LoadBalancerContext* wrapped_;
    Router::MetadataMatchCriteriaConstPtr metadata_match_;
  };

private:
  struct SubsetSelectorFallbackParams {
    envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
        LbSubsetSelectorFallbackPolicy fallback_policy_;
    const std::set<std::string>* fallback_keys_subset_ = nullptr;
  };

  struct SubsetSelectorMap {
    absl::node_hash_map<std::string, SubsetSelectorMapPtr> subset_keys_;
    SubsetSelectorFallbackParams fallback_params_;
  };

  class LbSubset {
  public:
    virtual ~LbSubset() = default;
    virtual HostConstSharedPtr chooseHost(LoadBalancerContext* context) const PURE;
    virtual void pushHost(uint32_t priority, HostSharedPtr host) PURE;
    virtual void finalize(uint32_t priority) PURE;
    virtual bool active() const PURE;
  };
  using LbSubsetPtr = std::unique_ptr<LbSubset>;

  class PriorityLbSubset : public LbSubset {
  public:
    PriorityLbSubset(const SubsetLoadBalancer& subset_lb, bool locality_weight_aware,
                     bool scale_locality_weight)
        : subset_(subset_lb, locality_weight_aware, scale_locality_weight) {}

    // Subset
    HostConstSharedPtr chooseHost(LoadBalancerContext* context) const override {
      return subset_.lb_->chooseHost(context);
    }
    void pushHost(uint32_t priority, HostSharedPtr host) override {
      while (host_sets_.size() <= priority) {
        host_sets_.push_back({HostHashSet(), HostHashSet()});
      }
      host_sets_[priority].second.emplace(std::move(host));
    }
    // Called after pushHost. Update subset by the hosts that pushed in the pushHost. If no any host
    // is pushed then subset_ will be set to empty.
    void finalize(uint32_t priority) override {
      while (host_sets_.size() <= priority) {
        host_sets_.push_back({HostHashSet(), HostHashSet()});
      }
      auto& [old_hosts, new_hosts] = host_sets_[priority];

      HostVector added;
      HostVector removed;

      for (const auto& host : old_hosts) {
        if (new_hosts.count(host) == 0) {
          removed.emplace_back(host);
        }
      }

      for (const auto& host : new_hosts) {
        if (old_hosts.count(host) == 0) {
          added.emplace_back(host);
        }
      }

      subset_.update(priority, new_hosts, added, removed);

      old_hosts.swap(new_hosts);
      new_hosts.clear();
    }

    bool active() const override { return !subset_.empty(); }

    std::vector<std::pair<HostHashSet, HostHashSet>> host_sets_;
    PrioritySubsetImpl subset_;
  };

  class SingleHostLbSubset : public LbSubset {
    // Subset
    HostConstSharedPtr chooseHost(LoadBalancerContext*) const override { return subset_; }
    // This is called at most once for every update for single host subset.
    void pushHost(uint32_t priority, HostSharedPtr host) override {
      new_hosts_[priority] = std::move(host);
    }
    // Called after pushHost. Update subset by the host that pushed in the pushHost. If no any host
    // is pushed then subset_ will be set to nullptr.
    void finalize(uint32_t priority) override {
      if (auto iter = new_hosts_.find(priority); iter == new_hosts_.end()) {
        // No any host for current subset and priority. Try remove record in the hosts_.
        hosts_.erase(priority);
      } else {
        // Single host is set for current subset and priority.
        hosts_[priority] = std::move(iter->second);
        new_hosts_.erase(priority);
      }

      if (hosts_.empty()) {
        subset_ = nullptr;
        return;
      }

      subset_ = hosts_.begin()->second;
    }
    bool active() const override { return subset_ != nullptr; }

    // We will update subsets for every priority separately and these simple map can help us
    // to ensure which priority has valid host quickly.
    std::map<uint32_t, HostSharedPtr> hosts_;
    std::map<uint32_t, HostSharedPtr> new_hosts_;
    HostConstSharedPtr subset_;
  };

  // Entry in the subset hierarchy.
  class LbSubsetEntry {
  public:
    LbSubsetEntry() = default;

    bool initialized() const { return lb_subset_ != nullptr; }
    bool active() const { return initialized() && lb_subset_->active(); }
    bool hasChildren() const { return !children_.empty(); }

    LbSubsetMap children_;

    // Only initialized if a match exists at this level.
    LbSubsetPtr lb_subset_;

    // Used to quick check if entry is single host subset entry or not.
    bool single_host_subset_{};
  };

  void initLbSubsetEntryOnce(LbSubsetEntryPtr& entry, bool single_host_subset);

  // Create filtered default subset (if necessary) and other subsets based on current hosts.
  void refreshSubsets();
  void refreshSubsets(uint32_t priority);

  // Called by HostSet::MemberUpdateCb
  void update(uint32_t priority, const HostVector& all_hosts);

  void updateFallbackSubset(uint32_t priority, const HostVector& all_hosts);
  void processSubsets(uint32_t priority, const HostVector& all_hosts);

  HostConstSharedPtr tryChooseHostFromContext(LoadBalancerContext* context, bool& host_chosen);

  absl::optional<SubsetSelectorFallbackParamsRef>
  tryFindSelectorFallbackParams(LoadBalancerContext* context);

  bool hostMatches(const SubsetMetadata& kvs, const Host& host);

  LbSubsetEntryPtr
  findSubset(const std::vector<Router::MetadataMatchCriterionConstSharedPtr>& matches);

  LbSubsetEntryPtr findOrCreateLbSubsetEntry(LbSubsetMap& subsets, const SubsetMetadata& kvs,
                                             uint32_t idx);
  void forEachSubset(LbSubsetMap& subsets, std::function<void(LbSubsetEntryPtr&)> cb);
  void purgeEmptySubsets(LbSubsetMap& subsets);

  std::vector<SubsetMetadata> extractSubsetMetadata(const std::set<std::string>& subset_keys,
                                                    const Host& host);
  std::string describeMetadata(const SubsetMetadata& kvs);
  HostConstSharedPtr chooseHostWithMetadataFallbacks(LoadBalancerContext* context,
                                                     const MetadataFallbacks& metadata_fallbacks);
  const ProtobufWkt::Value* getMetadataFallbackList(LoadBalancerContext* context) const;
  LoadBalancerContextWrapper removeMetadataFallbackList(LoadBalancerContext* context);

  ClusterLbStats& stats_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  TimeSource& time_source_;

  const envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy
      fallback_policy_;
  const envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetMetadataFallbackPolicy
      metadata_fallback_policy_;
  const SubsetMetadata default_subset_metadata_;
  std::vector<SubsetSelectorPtr> subset_selectors_;

  const PrioritySet& original_priority_set_;
  const PrioritySet* original_local_priority_set_;
  Common::CallbackHandlePtr original_priority_set_callback_handle_;

  ChildLoadBalancerCreatorPtr child_lb_creator_;

  LbSubsetEntryPtr subset_any_;
  LbSubsetEntryPtr subset_default_;

  // Reference to sub_set_any_ or subset_default_.
  LbSubsetEntryPtr fallback_subset_;
  LbSubsetEntryPtr panic_mode_subset_;

  // Forms a trie-like structure. Requires lexically sorted Host and Route metadata.
  LbSubsetMap subsets_;
  // Forms a trie-like structure of lexically sorted keys+fallback policy from subset
  // selectors configuration
  SubsetSelectorMapPtr selectors_;

  Stats::Gauge* single_duplicate_stat_{};

  // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
  const bool locality_weight_aware_ : 1;
  const bool scale_locality_weight_ : 1;
  const bool list_as_any_ : 1;
  const bool allow_redundant_keys_{};

  friend class SubsetLoadBalancerInternalStateTester;
};

} // namespace Upstream
} // namespace Envoy
