#pragma once

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"

#include "extensions/filters/common/rbac/engine.h"
#include "extensions/filters/common/rbac/matchers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class RoleBasedAccessControlEngineImpl : public RoleBasedAccessControlEngine {
public:
  RoleBasedAccessControlEngineImpl(const envoy::config::rbac::v2alpha::RBAC& rules);

  bool allowed(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata& metadata) const override;

private:
  const bool allowed_if_matched_;

  std::vector<PolicyMatcher> policies_;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
