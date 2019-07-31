#include "extensions/filters/common/rbac/engine_impl.h"

#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

RoleBasedAccessControlEngineImpl::RoleBasedAccessControlEngineImpl(
    const envoy::config::rbac::v2::RBAC& rules)
    : allowed_if_matched_(rules.action() ==
                          envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW) {
  // guard expression builder by presence of a condition in policies
  for (const auto& policy : rules.policies()) {
    if (policy.second.has_condition()) {
      builder_ = Expr::createBuilder();
      break;
    }
  }

  for (const auto& policy : rules.policies()) {
    policies_.emplace(policy.first, std::make_unique<PolicyMatcher>(policy.second, builder_.get()));
  }
}

bool RoleBasedAccessControlEngineImpl::allowed(const Network::Connection& connection,
                                               const Envoy::Http::HeaderMap& headers,
                                               const StreamInfo::StreamInfo& info,
                                               std::string* effective_policy_id) const {
  bool matched = false;

  for (auto it = policies_.begin(); it != policies_.end(); it++) {
    if (it->second->matches(connection, headers, info)) {
      matched = true;
      if (effective_policy_id != nullptr) {
        *effective_policy_id = it->first;
      }
      break;
    }
  }

  // only allowed if:
  //   - matched and ALLOW action
  //   - not matched and DENY action
  return matched == allowed_if_matched_;
}

bool RoleBasedAccessControlEngineImpl::allowed(const Network::Connection& connection,
                                               const StreamInfo::StreamInfo& info,
                                               std::string* effective_policy_id) const {
  static const Http::HeaderMapImpl* empty_header = new Http::HeaderMapImpl();
  return allowed(connection, *empty_header, info, effective_policy_id);
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
