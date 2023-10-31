#include "source/common/upstream/subset_lb_config.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Upstream {

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
      throwEnvoyExceptionOrPanic(
          "fallback_keys_subset can be set only for KEYS_SUBSET fallback_policy");
    }
    return;
  }

  // if KEYS_SUBSET fallback policy is selected, fallback_keys_subset must not be empty, because
  // it would be the same as not defining fallback policy at all (global fallback policy would be
  // used)
  if (fallback_keys_subset_.empty()) {
    throwEnvoyExceptionOrPanic("fallback_keys_subset cannot be empty");
  }

  // We allow only for a fallback to a subset of the selector keys because this is probably the
  // only use case that makes sense (fallback from more specific selector to less specific
  // selector). Potentially we can relax this constraint in the future if there will be a use case
  // for this.
  if (!std::includes(selector_keys_.begin(), selector_keys_.end(), fallback_keys_subset_.begin(),
                     fallback_keys_subset_.end())) {
    throwEnvoyExceptionOrPanic("fallback_keys_subset must be a subset of selector keys");
  }

  // Enforce that the fallback_keys_subset_ set is smaller than the selector_keys_ set. Otherwise
  // we could end up with a infinite recursion of SubsetLoadBalancer::chooseHost().
  if (selector_keys_.size() == fallback_keys_subset_.size()) {
    throwEnvoyExceptionOrPanic("fallback_keys_subset cannot be equal to keys");
  }
}

} // namespace Upstream
} // namespace Envoy
