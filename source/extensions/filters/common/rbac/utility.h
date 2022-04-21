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
std::unique_ptr<RoleBasedAccessControlEngine>
createEngine(const ConfigType& config, Server::Configuration::ServerFactoryContext& context,
             ProtobufMessage::ValidationVisitor& validation_visitor,
             ActionValidationVisitor& action_validation_visitor) {
  if (config.has_rules()) {
    if (config.has_matcher()) {
      ENVOY_LOG_MISC(warn, "matcher is ignored");
    }
    return std::make_unique<RoleBasedAccessControlEngineImpl>(config.rules(), validation_visitor,
                                                              EnforcementMode::Enforced);
  }
  if (config.has_matcher()) {
    return std::make_unique<RoleBasedAccessControlMatcherEngineImpl>(
        config.matcher(), context, action_validation_visitor, EnforcementMode::Enforced);
  }

  return nullptr;
}

template <class ConfigType>
std::unique_ptr<RoleBasedAccessControlEngine>
createShadowEngine(const ConfigType& config, Server::Configuration::ServerFactoryContext& context,
                   ProtobufMessage::ValidationVisitor& validation_visitor,
                   ActionValidationVisitor& action_validation_visitor) {
  if (config.has_shadow_rules()) {
    if (config.has_shadow_matcher()) {
      ENVOY_LOG_MISC(warn, "shadow matcher is ignored");
    }
    return std::make_unique<RoleBasedAccessControlEngineImpl>(
        config.shadow_rules(), validation_visitor, EnforcementMode::Shadow);
  }
  if (config.has_shadow_matcher()) {
    return std::make_unique<RoleBasedAccessControlMatcherEngineImpl>(
        config.shadow_matcher(), context, action_validation_visitor, EnforcementMode::Shadow);
  }

  return nullptr;
}

std::string responseDetail(const std::string& policy_id);

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
