#include "extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

RoleBasedAccessControlEngineImpl::RoleBasedAccessControlEngineImpl(
    const envoy::config::filter::http::rbac::v2::RBAC& config, bool disabled)
    : engine_disabled_(disabled),
      allowed_if_matched_(disabled ||
                          config.rules().action() ==
                              envoy::config::rbac::v2alpha::RBAC_Action::RBAC_Action_ALLOW) {
  if (disabled) {
    return;
  }

  for (const auto& policy : config.rules().policies()) {
    policies_.emplace_back(policy.second);
  }
}

RoleBasedAccessControlEngineImpl::RoleBasedAccessControlEngineImpl(
    const envoy::config::filter::http::rbac::v2::RBACPerRoute& per_route_config)
    : RoleBasedAccessControlEngineImpl(per_route_config.rbac(), per_route_config.disabled()) {}

bool RoleBasedAccessControlEngineImpl::allowed(const Network::Connection& connection,
                                               const Envoy::Http::HeaderMap& headers,
                                               bool& darklaunch_allowed) const {
  if (engine_disabled_) {
    return true;
  }

  bool matched = false;
  bool darklaunch_matched_ = false;
  for (const auto& policy : policies_) {
    if (policy.matches(connection, headers)) {
      if (policy.mode() ==
          envoy::config::rbac::v2alpha::PolicyEnforcementMode::POLICY_ENFORCEMENT_ENFORCED) {
        matched = true;
      }

      darklaunch_matched_ = true;
    }
  }

  darklaunch_allowed = darklaunch_matched_ == allowed_if_matched_;

  // only allowed if:
  //   - matched and ALLOW action
  //   - not matched and DENY action
  return matched == allowed_if_matched_;
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
