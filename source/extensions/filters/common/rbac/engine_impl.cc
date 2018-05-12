#include "extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

RBACEngineImpl::RBACEngineImpl(const envoy::config::filter::http::rbac::v2::RBAC& config,
                               bool disabled)
    : disabled_(disabled),
      allowed_(disabled || config.rules().action() ==
                               envoy::config::rbac::v2alpha::RBAC_Action::RBAC_Action_ALLOW) {
  if (disabled) {
    return;
  }

  for (const auto& policy : config.rules().policies()) {
    policies_.emplace_back(policy.second);
  }
}

RBACEngineImpl::RBACEngineImpl(const envoy::config::filter::http::rbac::v2::RBACPerRoute& config)
    : RBACEngineImpl(config.rbac(), config.disabled()) {}

bool RBACEngineImpl::allowed(const Network::Connection& connection) const {
  return isAllowed(connection);
}

bool RBACEngineImpl::allowed(const Network::Connection& connection,
                             const Envoy::Http::HeaderMap& headers) const {
  return isAllowed(connection, headers);
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
