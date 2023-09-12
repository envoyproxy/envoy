#include "source/common/upstream/load_balancer_config.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Upstream {

LegacyTypedRoundRobinLbConfig::LegacyTypedRoundRobinLbConfig(
    const envoy::config::cluster::v3::Cluster& config) {
  if (config.has_round_robin_lb_config()) {
    lb_config_ = config.round_robin_lb_config();
  }
}

TypedRoundRobinLbConfig::TypedRoundRobinLbConfig(
    const envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin& lb_config)
    : lb_config_(lb_config) {}

LegacyTypedLeastRequestLbConfig::LegacyTypedLeastRequestLbConfig(
    const envoy::config::cluster::v3::Cluster& config) {
  if (config.has_least_request_lb_config()) {
    lb_config_ = config.least_request_lb_config();
  }
}

TypedLeastRequestLbConfig::TypedLeastRequestLbConfig(
    const envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest& lb_config)
    : lb_config_(lb_config) {}

TypedRandomLbConfig::TypedRandomLbConfig(
    const envoy::extensions::load_balancing_policies::random::v3::Random& lb_config)
    : lb_config_(lb_config) {}

LegacyTypedMaglevLbConfig::LegacyTypedMaglevLbConfig(
    const envoy::config::cluster::v3::Cluster& config) {
  if (config.has_maglev_lb_config()) {
    lb_config_ = config.maglev_lb_config();
  }
}

TypedMaglevLbConfig::TypedMaglevLbConfig(
    const envoy::extensions::load_balancing_policies::maglev::v3::Maglev& config)
    : lb_config_(config) {}

LegacyTypedRingHashLbConfig::LegacyTypedRingHashLbConfig(
    const envoy::config::cluster::v3::Cluster& config) {
  if (config.has_ring_hash_lb_config()) {
    lb_config_ = config.ring_hash_lb_config();
  }
}

TypedRingHashLbConfig::TypedRingHashLbConfig(
    const envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash& config)
    : lb_config_(config) {}

SubsetSelectorImpl::SubsetSelectorImpl(
    const Protobuf::RepeatedPtrField<std::string>& selector_keys,
    envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
        LbSubsetSelectorFallbackPolicy fallback_policy,
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
    const SubsetLoadbalancingPolicyProto& subset_config,
    ProtobufMessage::ValidationVisitor& visitor)
    : subset_info_(subset_config) {

  absl::InlinedVector<absl::string_view, 4> missing_policies;

  for (const auto& policy : subset_config.subset_lb_policy().policies()) {
    auto* factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(
        policy.typed_extension_config(), /*is_optional=*/true);

    if (factory != nullptr) {
      // Load and validate the configuration.
      auto sub_lb_proto_message = factory->createEmptyConfigProto();
      Config::Utility::translateOpaqueConfig(policy.typed_extension_config().typed_config(),
                                             visitor, *sub_lb_proto_message);

      sub_load_balancer_config_ = factory->loadConfig(std::move(sub_lb_proto_message), visitor);
      sub_load_balancer_factory_ = factory;
      break;
    }

    missing_policies.push_back(policy.typed_extension_config().name());
  }

  if (sub_load_balancer_factory_ == nullptr) {
    throw EnvoyException(fmt::format("cluster: didn't find a registered load balancer factory "
                                     "implementation for subset lb with names from [{}]",
                                     absl::StrJoin(missing_policies, ", ")));
  }
}

} // namespace Upstream
} // namespace Envoy
