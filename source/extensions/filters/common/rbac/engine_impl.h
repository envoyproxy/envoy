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
  // Constructs an engine based off the rules. If disable_http_rules is true, header and metadata
  // rule will be treated as always matched. This is useful for network RBAC filter where header and
  // metadata are not available.
  RoleBasedAccessControlEngineImpl(const envoy::config::rbac::v2alpha::RBAC& rules,
                                   bool disable_http_rules = false);

  bool allowed(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata& metadata,
               std::string* effective_policy_id) const override;

private:
  const bool allowed_if_matched_;

  std::map<std::string, PolicyMatcher> policies_;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
