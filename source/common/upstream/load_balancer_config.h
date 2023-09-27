#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/load_balancing_policies/common/v3/common.pb.h"
#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"
#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.validate.h"
#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.h"
#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.validate.h"
#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.h"
#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.validate.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.validate.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.validate.h"
#include "envoy/extensions/load_balancing_policies/subset/v3/subset.pb.h"
#include "envoy/extensions/load_balancing_policies/subset/v3/subset.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

namespace Envoy {
namespace Upstream {

/**
 * Load balancer config that used to wrap the legacy proto config.
 */
class LegacyTypedRoundRobinLbConfig : public LoadBalancerConfig {
public:
  LegacyTypedRoundRobinLbConfig(const envoy::config::cluster::v3::Cluster& config);

  absl::optional<envoy::config::cluster::v3::Cluster::RoundRobinLbConfig> lb_config_;
};

/**
 * Load balancer config that used to wrap the proto config.
 */
class TypedRoundRobinLbConfig : public LoadBalancerConfig {
public:
  TypedRoundRobinLbConfig(
      const envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin& lb_config);

  const envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin lb_config_;
};

/**
 * Load balancer config that used to wrap the legacy least request config.
 */
class LegacyTypedLeastRequestLbConfig : public LoadBalancerConfig {
public:
  LegacyTypedLeastRequestLbConfig(const envoy::config::cluster::v3::Cluster& config);

  absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig> lb_config_;
};

/**
 * Load balancer config that used to wrap the least request config.
 */
class TypedLeastRequestLbConfig : public LoadBalancerConfig {
public:
  TypedLeastRequestLbConfig(
      const envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest& lb_config);

  const envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest lb_config_;
};

/**
 * Load balancer config that used to wrap the legacy random config.
 */
class LegacyTypedRandomLbConfig : public LoadBalancerConfig {};

/**
 * Load balancer config that used to wrap the random config.
 */
class TypedRandomLbConfig : public LoadBalancerConfig {
public:
  TypedRandomLbConfig(
      const envoy::extensions::load_balancing_policies::random::v3::Random& lb_config);

  const envoy::extensions::load_balancing_policies::random::v3::Random lb_config_;
};

/**
 * Load balancer config that used to wrap legacy maglev config.
 */
class LegacyTypedMaglevLbConfig : public Upstream::LoadBalancerConfig {
public:
  LegacyTypedMaglevLbConfig(const envoy::config::cluster::v3::Cluster& config);

  absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig> lb_config_;
};

/**
 * Load balancer config that used to wrap typed maglev config.
 */
class TypedMaglevLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedMaglevLbConfig(const envoy::extensions::load_balancing_policies::maglev::v3::Maglev& config);

  const envoy::extensions::load_balancing_policies::maglev::v3::Maglev lb_config_;
};

/**
 * Load balancer config that used to wrap legacy ring hash config.
 */
class LegacyTypedRingHashLbConfig : public Upstream::LoadBalancerConfig {
public:
  LegacyTypedRingHashLbConfig(const envoy::config::cluster::v3::Cluster& config);

  absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig> lb_config_;
};

/**
 * Load balancer config that used to wrap typed ring hash config.
 */
class TypedRingHashLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedRingHashLbConfig(
      const envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash& config);

  const envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash lb_config_;
};

/**
 * Implementation of SubsetSelector. This is part of subset load balancer config and is used to
 * store config of single selector.
 */
class SubsetSelectorImpl : public SubsetSelector {
public:
  SubsetSelectorImpl(const Protobuf::RepeatedPtrField<std::string>& selector_keys,
                     envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
                         LbSubsetSelectorFallbackPolicy fallback_policy,
                     const Protobuf::RepeatedPtrField<std::string>& fallback_keys_subset,
                     bool single_host_per_subset);

  // SubsetSelector
  const std::set<std::string>& selectorKeys() const override { return selector_keys_; }
  envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
      LbSubsetSelectorFallbackPolicy
      fallbackPolicy() const override {
    return fallback_policy_;
  }
  const std::set<std::string>& fallbackKeysSubset() const override { return fallback_keys_subset_; }
  bool singleHostPerSubset() const override { return single_host_per_subset_; }

private:
  const std::set<std::string> selector_keys_;
  const std::set<std::string> fallback_keys_subset_;
  // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
  const envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
      LbSubsetSelectorFallbackPolicy fallback_policy_;
  const bool single_host_per_subset_ : 1;
};

using SubsetLoadbalancingPolicyProto =
    envoy::extensions::load_balancing_policies::subset::v3::Subset;
using LegacySubsetLoadbalancingPolicyProto = envoy::config::cluster::v3::Cluster::LbSubsetConfig;

/**
 * Implementation of LoadBalancerSubsetInfo. Both the legacy and extension subset proto configs
 * are converted to this class.
 */
class LoadBalancerSubsetInfoImpl : public LoadBalancerSubsetInfo {
public:
  using FallbackPolicy =
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy;
  using MetadataFallbackPolicy =
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetMetadataFallbackPolicy;
  using SubsetFallbackPolicy = envoy::config::cluster::v3::Cluster::LbSubsetConfig::
      LbSubsetSelector::LbSubsetSelectorFallbackPolicy;

  LoadBalancerSubsetInfoImpl(const SubsetLoadbalancingPolicyProto& subset_config)
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
        subset_selectors_.emplace_back(std::make_shared<Upstream::SubsetSelectorImpl>(
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

  LoadBalancerSubsetInfoImpl(const LegacySubsetLoadbalancingPolicyProto& subset_config)
      : default_subset_(subset_config.default_subset()),
        fallback_policy_(subset_config.fallback_policy()),
        metadata_fallback_policy_(subset_config.metadata_fallback_policy()),
        enabled_(!subset_config.subset_selectors().empty()),
        locality_weight_aware_(subset_config.locality_weight_aware()),
        scale_locality_weight_(subset_config.scale_locality_weight()),
        panic_mode_any_(subset_config.panic_mode_any()), list_as_any_(subset_config.list_as_any()) {
    for (const auto& subset : subset_config.subset_selectors()) {
      if (!subset.keys().empty()) {
        subset_selectors_.emplace_back(std::make_shared<SubsetSelectorImpl>(
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
  const std::vector<Upstream::SubsetSelectorPtr>& subsetSelectors() const override {
    return subset_selectors_;
  }
  bool localityWeightAware() const override { return locality_weight_aware_; }
  bool scaleLocalityWeight() const override { return scale_locality_weight_; }
  bool panicModeAny() const override { return panic_mode_any_; }
  bool listAsAny() const override { return list_as_any_; }
  bool allowRedundantKeys() const override { return allow_redundant_keys_; }

private:
  const ProtobufWkt::Struct default_subset_;
  std::vector<Upstream::SubsetSelectorPtr> subset_selectors_;
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
using DefaultLoadBalancerSubsetInfoImpl = ConstSingleton<LoadBalancerSubsetInfoImpl>;

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

} // namespace Upstream
} // namespace Envoy
