#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/load_balancing_policies/common/v3/common.pb.h"
#include "envoy/extensions/load_balancing_policies/common/v3/common.pb.validate.h"
#include "envoy/extensions/load_balancing_policies/subset/v3/subset.pb.h"
#include "envoy/extensions/load_balancing_policies/subset/v3/subset.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

namespace Envoy {
namespace Upstream {

using SubsetLbConfigProto = envoy::extensions::load_balancing_policies::subset::v3::Subset;
using ClusterProto = envoy::config::cluster::v3::Cluster;
using LegacySubsetLbConfigProto = envoy::config::cluster::v3::Cluster::LbSubsetConfig;

/**
 * This is part of subset load balancer config and is used to store config of single selector.
 */
class SubsetSelector {
public:
  SubsetSelector(const Protobuf::RepeatedPtrField<std::string>& selector_keys,
                 envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
                     LbSubsetSelectorFallbackPolicy fallback_policy,
                 const Protobuf::RepeatedPtrField<std::string>& fallback_keys_subset,
                 bool single_host_per_subset);

  // SubsetSelector
  const std::set<std::string>& selectorKeys() const { return selector_keys_; }
  envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
      LbSubsetSelectorFallbackPolicy
      fallbackPolicy() const {
    return fallback_policy_;
  }
  const std::set<std::string>& fallbackKeysSubset() const { return fallback_keys_subset_; }
  bool singleHostPerSubset() const { return single_host_per_subset_; }

private:
  const std::set<std::string> selector_keys_;
  const std::set<std::string> fallback_keys_subset_;
  // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
  const envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
      LbSubsetSelectorFallbackPolicy fallback_policy_;
  const bool single_host_per_subset_ : 1;
};

using SubsetSelectorPtr = std::shared_ptr<SubsetSelector>;

/**
 * Load Balancer subset configuration.
 */
class LoadBalancerSubsetInfo {
public:
  virtual ~LoadBalancerSubsetInfo() = default;

  /**
   * @return bool true if load balancer subsets are configured.
   */
  virtual bool isEnabled() const PURE;

  /**
   * @return LbSubsetFallbackPolicy the fallback policy used when
   * route metadata does not match any subset.
   */
  virtual envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy
  fallbackPolicy() const PURE;

  /**
   * @return LbSubsetMetadataFallbackPolicy the fallback policy used to try different route metadata
   * until a host is found
   */
  virtual envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetMetadataFallbackPolicy
  metadataFallbackPolicy() const PURE;

  /**
   * @return ProtobufWkt::Struct the struct describing the metadata for a
   *         host to be included in the default subset.
   */
  virtual const ProtobufWkt::Struct& defaultSubset() const PURE;

  /*
   * @return const std:vector<std:set<std::string>>& a vector of
   * sorted keys used to define load balancer subsets.
   */
  virtual const std::vector<SubsetSelectorPtr>& subsetSelectors() const PURE;

  /*
   * @return bool whether routing to subsets should take locality weights into account.
   */
  virtual bool localityWeightAware() const PURE;

  /*
   * @return bool whether the locality weights should be scaled to compensate for the
   * fraction of hosts removed from the original host set.
   */
  virtual bool scaleLocalityWeight() const PURE;

  /*
   * @return bool whether to attempt to select a host from the entire cluster if host
   * selection from the fallback subset fails.
   */
  virtual bool panicModeAny() const PURE;

  /*
   * @return bool whether matching metadata should attempt to match against any of the
   * elements in a list value defined in endpoint metadata.
   */
  virtual bool listAsAny() const PURE;

  /*
   * @return bool whether redundant key/value pairs is allowed in the request metadata.
   */
  virtual bool allowRedundantKeys() const PURE;
};

using LoadBalancerSubsetInfoPtr = std::unique_ptr<LoadBalancerSubsetInfo>;

/**
 * Both the legacy and extension subset proto configs are converted to this class.
 */
class LoadBalancerSubsetInfoImpl : public LoadBalancerSubsetInfo {
public:
  using FallbackPolicy =
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy;
  using MetadataFallbackPolicy =
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetMetadataFallbackPolicy;
  using SubsetFallbackPolicy = envoy::config::cluster::v3::Cluster::LbSubsetConfig::
      LbSubsetSelector::LbSubsetSelectorFallbackPolicy;

  LoadBalancerSubsetInfoImpl(const SubsetLbConfigProto& subset_config)
      : default_subset_(subset_config.default_subset()),
        fallback_policy_(static_cast<FallbackPolicy>(subset_config.fallback_policy())),
        metadata_fallback_policy_(
            static_cast<MetadataFallbackPolicy>(subset_config.metadata_fallback_policy())),
        enabled_(!subset_config.subset_selectors().empty()),
        locality_weight_aware_(subset_config.locality_weight_aware()),
        scale_locality_weight_(subset_config.scale_locality_weight()),
        panic_mode_any_(subset_config.panic_mode_any()), list_as_any_(subset_config.list_as_any()),
        allow_redundant_keys_(subset_config.allow_redundant_keys()) {
    for (const auto& subset : subset_config.subset_selectors()) {
      if (!subset.keys().empty()) {
        subset_selectors_.emplace_back(std::make_shared<SubsetSelector>(
            subset.keys(), static_cast<SubsetFallbackPolicy>(subset.fallback_policy()),
            subset.fallback_keys_subset(), subset.single_host_per_subset()));
      }
    }

    if (allow_redundant_keys_) {
      // Sort subset selectors by number of keys, descending. This will ensure that the longest
      // matching subset selector will be at the beginning of the list.
      std::stable_sort(subset_selectors_.begin(), subset_selectors_.end(),
                       [](const SubsetSelectorPtr& a, const SubsetSelectorPtr& b) -> bool {
                         return a->selectorKeys().size() > b->selectorKeys().size();
                       });
    }
  }

  LoadBalancerSubsetInfoImpl(const LegacySubsetLbConfigProto& subset_config)
      : default_subset_(subset_config.default_subset()),
        fallback_policy_(subset_config.fallback_policy()),
        metadata_fallback_policy_(subset_config.metadata_fallback_policy()),
        enabled_(!subset_config.subset_selectors().empty()),
        locality_weight_aware_(subset_config.locality_weight_aware()),
        scale_locality_weight_(subset_config.scale_locality_weight()),
        panic_mode_any_(subset_config.panic_mode_any()), list_as_any_(subset_config.list_as_any()) {
    for (const auto& subset : subset_config.subset_selectors()) {
      if (!subset.keys().empty()) {
        subset_selectors_.emplace_back(std::make_shared<SubsetSelector>(
            subset.keys(), subset.fallback_policy(), subset.fallback_keys_subset(),
            subset.single_host_per_subset()));
      }
    }
  }
  LoadBalancerSubsetInfoImpl()
      : LoadBalancerSubsetInfoImpl(
            envoy::config::cluster::v3::Cluster::LbSubsetConfig::default_instance()) {}

  // Upstream::LoadBalancerSubsetInfo
  bool isEnabled() const override { return enabled_; }
  FallbackPolicy fallbackPolicy() const override { return fallback_policy_; }
  MetadataFallbackPolicy metadataFallbackPolicy() const override {
    return metadata_fallback_policy_;
  }
  const ProtobufWkt::Struct& defaultSubset() const override { return default_subset_; }
  const std::vector<SubsetSelectorPtr>& subsetSelectors() const override {
    return subset_selectors_;
  }
  bool localityWeightAware() const override { return locality_weight_aware_; }
  bool scaleLocalityWeight() const override { return scale_locality_weight_; }
  bool panicModeAny() const override { return panic_mode_any_; }
  bool listAsAny() const override { return list_as_any_; }
  bool allowRedundantKeys() const override { return allow_redundant_keys_; }

private:
  const ProtobufWkt::Struct default_subset_;
  std::vector<SubsetSelectorPtr> subset_selectors_;
  // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
  const FallbackPolicy fallback_policy_;
  const MetadataFallbackPolicy metadata_fallback_policy_;
  const bool enabled_ : 1;
  const bool locality_weight_aware_ : 1;
  const bool scale_locality_weight_ : 1;
  const bool panic_mode_any_ : 1;
  const bool list_as_any_ : 1;
  const bool allow_redundant_keys_{};
};

using DefaultLoadBalancerSubsetInfo = ConstSingleton<LoadBalancerSubsetInfoImpl>;

class SubsetLoadBalancerConfig : public Upstream::LoadBalancerConfig {
public:
  SubsetLoadBalancerConfig(Upstream::LoadBalancerFactoryContext& lb_factory_context,
                           const SubsetLbConfigProto& subset_config,
                           ProtobufMessage::ValidationVisitor& visitor);
  SubsetLoadBalancerConfig(Upstream::LoadBalancerFactoryContext& lb_factory_context,
                           const ClusterProto& cluster,
                           ProtobufMessage::ValidationVisitor& visitor);
  SubsetLoadBalancerConfig(LoadBalancerSubsetInfoPtr subset_info,
                           TypedLoadBalancerFactory* child_factory,
                           LoadBalancerConfigPtr child_config);

  Upstream::ThreadAwareLoadBalancerPtr
  createLoadBalancer(const Upstream::ClusterInfo& cluster_info,
                     const Upstream::PrioritySet& child_priority_set, Runtime::Loader& runtime,
                     Random::RandomGenerator& random, TimeSource& time_source) const {
    return child_lb_factory_->create(
        makeOptRefFromPtr<const Upstream::LoadBalancerConfig>(child_lb_config_.get()), cluster_info,
        child_priority_set, runtime, random, time_source);
  }
  std::string childLoadBalancerName() const { return child_lb_factory_->name(); }

  const LoadBalancerSubsetInfo& subsetInfo() const { return *subset_info_; }

private:
  LoadBalancerSubsetInfoPtr subset_info_;
  Upstream::TypedLoadBalancerFactory* child_lb_factory_{};
  Upstream::LoadBalancerConfigPtr child_lb_config_;
};

} // namespace Upstream
} // namespace Envoy
