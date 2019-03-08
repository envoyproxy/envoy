#include "extensions/filters/common/rbac/engine_impl.h"

#include "common/http/header_map_impl.h"

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
    policies_.insert(std::make_pair(policy.first, policy.second));
  }
}

bool RoleBasedAccessControlEngineImpl::allowed(const Network::Connection& connection,
                                               const Envoy::Http::HeaderMap& headers,
                                               const envoy::api::v2::core::Metadata& metadata,
                                               std::string* effective_policy_id) const {
  bool matched = false;

  for (auto it = policies_.begin(); it != policies_.end(); it++) {
    if (it->second.matches(connection, headers, metadata)) {
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
                                               const envoy::api::v2::core::Metadata& metadata,
                                               std::string* effective_policy_id) const {
  static const Http::HeaderMapImpl* empty_header = new Http::HeaderMapImpl();
  return allowed(connection, *empty_header, metadata, effective_policy_id);
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
