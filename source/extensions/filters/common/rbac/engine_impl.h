#pragma once

#include "envoy/config/rbac/v3/rbac.pb.h"

#include "extensions/filters/common/rbac/engine.h"
#include "extensions/filters/common/rbac/matchers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class DynamicMetadataKeys {
public:
  const std::string ShadowEffectivePolicyIdField{"shadow_effective_policy_id"};
  const std::string ShadowEngineResultField{"shadow_engine_result"};
  const std::string EngineResultAllowed{"allowed"};
  const std::string EngineResultDenied{"denied"};
  const std::string AccessLogKey{"access_log_hint"};
  const std::string CommonNamespace{"envoy.common"};
};

using DynamicMetadataKeysSingleton = ConstSingleton<DynamicMetadataKeys>;

enum class EnforcementMode { Enforced, Shadow };

class RoleBasedAccessControlEngineImpl : public RoleBasedAccessControlEngine, NonCopyable {
public:
  RoleBasedAccessControlEngineImpl(const envoy::config::rbac::v3::RBAC& rules,
                                   const EnforcementMode mode = EnforcementMode::Enforced);

  bool handleAction(const Network::Connection& connection,
                    const Envoy::Http::RequestHeaderMap& headers, StreamInfo::StreamInfo& info,
                    std::string* effective_policy_id) const override;

  bool handleAction(const Network::Connection& connection, StreamInfo::StreamInfo& info,
                    std::string* effective_policy_id) const override;

private:
  // Checks whether the request matches any policies
  bool checkPolicyMatch(const Network::Connection& connection, const StreamInfo::StreamInfo& info,
                        const Envoy::Http::RequestHeaderMap& headers,
                        std::string* effective_policy_id) const;

  const envoy::config::rbac::v3::RBAC::Action action_;
  const EnforcementMode mode_;

  std::map<std::string, std::unique_ptr<PolicyMatcher>> policies_;

  Protobuf::Arena constant_arena_;
  Expr::BuilderPtr builder_;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
