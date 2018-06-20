#include "extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

RoleBasedAccessControlEngineImpl::RoleBasedAccessControlEngineImpl(
    const envoy::config::rbac::v2alpha::RBAC& rules)
    : allowed_if_matched_(rules.action() ==
                          envoy::config::rbac::v2alpha::RBAC_Action::RBAC_Action_ALLOW) {
  for (const auto& policy : rules.policies()) {
    policies_.emplace_back(policy.second);
  }
}

bool RoleBasedAccessControlEngineImpl::allowed(const Network::Connection& connection,
                                               const Envoy::Http::HeaderMap& headers) const {
  bool matched = false;
  for (const auto& policy : policies_) {
    if (policy.matches(connection, headers)) {
      matched = true;
      break;
    }
  }

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
