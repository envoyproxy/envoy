#pragma once

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"

#include "extensions/filters/common/rbac/engine.h"
#include "extensions/filters/common/rbac/matchers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class RBACEngineImpl : public RBACEngine {
public:
  RBACEngineImpl(const envoy::config::filter::http::rbac::v2::RBAC&, bool disabled);
  RBACEngineImpl(const envoy::config::filter::http::rbac::v2::RBACPerRoute&);
  ~RBACEngineImpl() {}

  bool allowed(const Network::Connection&) const override;
  bool allowed(const Network::Connection&, const Envoy::Http::HeaderMap&) const override;

private:
  const bool disabled_;
  const bool allowed_;
  std::vector<PolicyMatcher> policies_;

  template <class... Args> bool isAllowed(Args&&... args) const {
    if (disabled_) {
      return true;
    }

    bool matched = false;
    for (const auto& policy : policies_) {
      if (policy.matches(std::forward<Args>(args)...)) {
        matched = true;
        break;
      }
    }

    // only allowed if:
    //   - matched and ALLOW action
    //   - not matched and DENY action
    return matched == allowed_;
  }
}; // namespace RBAC

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
