#pragma once

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"
#include "envoy/config/filter/network/rbac/v2/rbac.pb.h"
#include "envoy/stats/stats_macros.h"

#include "common/singleton/const_singleton.h"

#include "extensions/filters/common/rbac/engine_impl.h"

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
};

typedef ConstSingleton<DynamicMetadataKeys> DynamicMetadataKeysSingleton;

/**
 * All stats for the RBAC filter. @see stats_macros.h
 */
// clang-format off
#define ALL_RBAC_FILTER_STATS(COUNTER)                                                             \
  COUNTER(allowed)                                                                                 \
  COUNTER(denied)                                                                                  \
  COUNTER(shadow_allowed)                                                                          \
  COUNTER(shadow_denied)
// clang-format on

/**
 * Wrapper struct for RBAC filter stats. @see stats_macros.h
 */
struct RoleBasedAccessControlFilterStats {
  ALL_RBAC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

RoleBasedAccessControlFilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

enum class EnforcementMode { Enforced, Shadow };

template <class ConfigType>
absl::optional<RoleBasedAccessControlEngineImpl> createEngine(const ConfigType& config) {
  return config.has_rules() ? absl::make_optional<RoleBasedAccessControlEngineImpl>(config.rules())
                            : absl::nullopt;
}

template <class ConfigType>
absl::optional<RoleBasedAccessControlEngineImpl> createShadowEngine(const ConfigType& config) {
  return config.has_shadow_rules()
             ? absl::make_optional<RoleBasedAccessControlEngineImpl>(config.shadow_rules())
             : absl::nullopt;
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
