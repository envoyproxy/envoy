#pragma once

#include "envoy/config/rbac/v3/rbac.pb.h"

#include "extensions/filters/common/rbac/engine.h"
#include "extensions/filters/common/rbac/matchers.h"
#include "extensions/filters/common/rbac/utility.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

enum class EnforcementMode { Enforced, Shadow };

class RoleBasedAccessControlEngineImpl : public RoleBasedAccessControlEngine, NonCopyable {
public:
  RoleBasedAccessControlEngineImpl(const envoy::config::rbac::v3::RBAC& rules,
                                   const EnforcementMode mode = EnforcementMode::Enforced);

  bool handleAction(const Network::Connection& connection,
                    const Envoy::Http::RequestHeaderMap& headers, StreamInfo::StreamInfo& info,
                    std::string* effective_policy_id) override;

  bool handleAction(const Network::Connection& connection, StreamInfo::StreamInfo& info,
                    std::string* effective_policy_id) override;

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

template <class ConfigType>
std::unique_ptr<RoleBasedAccessControlEngineImpl> createEngine(const ConfigType& config) {
  return config.has_rules() ? std::make_unique<RoleBasedAccessControlEngineImpl>(
                                  config.rules(), EnforcementMode::Enforced)
                            : nullptr;
}

template <class ConfigType>
std::unique_ptr<RoleBasedAccessControlEngineImpl> createShadowEngine(const ConfigType& config) {
  return config.has_shadow_rules() ? std::make_unique<RoleBasedAccessControlEngineImpl>(
                                         config.shadow_rules(), EnforcementMode::Shadow)
                                   : nullptr;
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
