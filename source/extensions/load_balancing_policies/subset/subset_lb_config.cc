#include "source/extensions/load_balancing_policies/subset/subset_lb_config.h"

#include "source/common/config/utility.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

SubsetSelector::SubsetSelector(const Protobuf::RepeatedPtrField<std::string>& selector_keys,
                               envoy::config::cluster::v3::Cluster::LbSubsetConfig::
                                   LbSubsetSelector::LbSubsetSelectorFallbackPolicy fallback_policy,
                               const Protobuf::RepeatedPtrField<std::string>& fallback_keys_subset,
                               bool single_host_per_subset)
    : selector_keys_(selector_keys.begin(), selector_keys.end()),
      fallback_keys_subset_(fallback_keys_subset.begin(), fallback_keys_subset.end()),
      fallback_policy_(fallback_policy), single_host_per_subset_(single_host_per_subset) {

  if (fallback_policy_ !=
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET) {
    // defining fallback_keys_subset_ for a fallback policy other than KEYS_SUBSET doesn't have
    // any effect and it is probably a user mistake. We should let the user know about it.
    if (!fallback_keys_subset_.empty()) {
      throw EnvoyException("fallback_keys_subset can be set only for KEYS_SUBSET fallback_policy");
    }
    return;
  }

  // if KEYS_SUBSET fallback policy is selected, fallback_keys_subset must not be empty, because
  // it would be the same as not defining fallback policy at all (global fallback policy would be
  // used)
  if (fallback_keys_subset_.empty()) {
    throw EnvoyException("fallback_keys_subset cannot be empty");
  }

  // We allow only for a fallback to a subset of the selector keys because this is probably the
  // only use case that makes sense (fallback from more specific selector to less specific
  // selector). Potentially we can relax this constraint in the future if there will be a use case
  // for this.
  if (!std::includes(selector_keys_.begin(), selector_keys_.end(), fallback_keys_subset_.begin(),
                     fallback_keys_subset_.end())) {
    throw EnvoyException("fallback_keys_subset must be a subset of selector keys");
  }

  // Enforce that the fallback_keys_subset_ set is smaller than the selector_keys_ set. Otherwise
  // we could end up with a infinite recursion of SubsetLoadBalancer::chooseHost().
  if (selector_keys_.size() == fallback_keys_subset_.size()) {
    throw EnvoyException("fallback_keys_subset cannot be equal to keys");
  }
}

SubsetLoadBalancerConfig::SubsetLoadBalancerConfig(
    Upstream::LoadBalancerFactoryContext& lb_factory_context,
    const SubsetLbConfigProto& subset_config, ProtobufMessage::ValidationVisitor& visitor)
    : subset_info_(std::make_unique<LoadBalancerSubsetInfoImpl>(subset_config)) {
  absl::InlinedVector<absl::string_view, 4> missing_policies;

  for (const auto& policy : subset_config.subset_lb_policy().policies()) {
    auto* factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(
        policy.typed_extension_config(), /*is_optional=*/true);

    if (factory != nullptr) {
      // Load and validate the configuration.
      auto sub_lb_proto_message = factory->createEmptyConfigProto();
      Config::Utility::translateOpaqueConfig(policy.typed_extension_config().typed_config(),
                                             visitor, *sub_lb_proto_message);

      child_lb_config_ = factory->loadConfig(lb_factory_context, *sub_lb_proto_message, visitor);
      child_lb_factory_ = factory;
      break;
    }

    missing_policies.push_back(policy.typed_extension_config().name());
  }

  if (child_lb_factory_ == nullptr) {
    throw EnvoyException(fmt::format("cluster: didn't find a registered load balancer factory "
                                     "implementation for subset lb with names from [{}]",
                                     absl::StrJoin(missing_policies, ", ")));
  }
}

SubsetLoadBalancerConfig::SubsetLoadBalancerConfig(
    Upstream::LoadBalancerFactoryContext& lb_factory_context, const ClusterProto& cluster,
    ProtobufMessage::ValidationVisitor& visitor)
    : subset_info_(std::make_unique<LoadBalancerSubsetInfoImpl>(cluster.lb_subset_config())) {
  ASSERT(subset_info_->isEnabled());

  auto sub_lb_pair = LegacyLbPolicyConfigHelper::getTypedLbConfigFromLegacyProtoWithoutSubset(
      lb_factory_context, cluster, visitor);
  if (!sub_lb_pair.ok()) {
    throw EnvoyException(std::string(sub_lb_pair.status().message()));
  }

  child_lb_factory_ = sub_lb_pair->factory;
  ASSERT(child_lb_factory_ != nullptr);
  child_lb_config_ = std::move(sub_lb_pair->config);
}

SubsetLoadBalancerConfig::SubsetLoadBalancerConfig(
    std::unique_ptr<LoadBalancerSubsetInfo> subset_info, TypedLoadBalancerFactory* child_factory,
    LoadBalancerConfigPtr child_config)
    : subset_info_(std::move(subset_info)), child_lb_factory_(child_factory),
      child_lb_config_(std::move(child_config)) {}

} // namespace Upstream
} // namespace Envoy
