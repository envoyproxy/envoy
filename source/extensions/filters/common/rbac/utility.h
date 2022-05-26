#pragma once

#include "envoy/stats/stats_macros.h"

#include "source/common/common/fmt.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

/**
 * All stats for the enforced rules in RBAC filter. @see stats_macros.h
 */
#define ENFORCE_RBAC_FILTER_STATS(COUNTER)                                                         \
  COUNTER(allowed)                                                                                 \
  COUNTER(denied)

/**
 * All stats for the shadow rules in RBAC filter. @see stats_macros.h
 */
#define SHADOW_RBAC_FILTER_STATS(COUNTER)                                                          \
  COUNTER(shadow_allowed)                                                                          \
  COUNTER(shadow_denied)

/**
 * Wrapper struct for shadow rules in RBAC filter stats. @see stats_macros.h
 */
struct RoleBasedAccessControlFilterStats {
  ENFORCE_RBAC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
  SHADOW_RBAC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

RoleBasedAccessControlFilterStats
generateStats(const std::string& prefix, const std::string& shadow_prefix, Stats::Scope& scope);

template <class ConfigType>
std::unique_ptr<RoleBasedAccessControlEngineImpl>
createEngine(const ConfigType& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  return config.has_rules() ? std::make_unique<RoleBasedAccessControlEngineImpl>(
                                  config.rules(), validation_visitor, EnforcementMode::Enforced)
                            : nullptr;
}

template <class ConfigType>
std::unique_ptr<RoleBasedAccessControlEngineImpl>
createShadowEngine(const ConfigType& config,
                   ProtobufMessage::ValidationVisitor& validation_visitor) {
  return config.has_shadow_rules()
             ? std::make_unique<RoleBasedAccessControlEngineImpl>(
                   config.shadow_rules(), validation_visitor, EnforcementMode::Shadow)
             : nullptr;
}

std::string responseDetail(const std::string& policy_id);

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
