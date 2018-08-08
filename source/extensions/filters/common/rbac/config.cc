#include "extensions/filters/common/rbac/config.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

#define INIT_RBAC_ENGINES(config, disable_http_rules)                                              \
  engine_(config.has_rules() ? absl::make_optional<RoleBasedAccessControlEngineImpl>(              \
                                   config.rules(), disable_http_rules)                             \
                             : absl::nullopt),                                                     \
      shadow_engine_(config.has_shadow_rules()                                                     \
                         ? absl::make_optional<RoleBasedAccessControlEngineImpl>(                  \
                               config.shadow_rules(), disable_http_rules)                          \
                         : absl::nullopt)

RoleBasedAccessControlRouteSpecificFilterConfig::RoleBasedAccessControlRouteSpecificFilterConfig(
    const envoy::config::filter::http::rbac::v2::RBACPerRoute& per_route_config)
    : INIT_RBAC_ENGINES(per_route_config.rbac(), false /* disable_http_rules */) {}

RoleBasedAccessControlFilterStats
RoleBasedAccessControlFilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "rbac.";
  return {ALL_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope)
    : stats_(generateStats(stats_prefix, scope)),
      INIT_RBAC_ENGINES(proto_config, false /* disable_http_rules */) {}

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::config::filter::network::rbac::v2::RBAC& proto_config, Stats::Scope& scope)
    : stats_(generateStats(proto_config.stat_prefix(), scope)),
      INIT_RBAC_ENGINES(proto_config, true /* disable_http_rules */) {}

const absl::optional<RoleBasedAccessControlEngineImpl>&
RoleBasedAccessControlFilterConfig::engine(const Router::RouteConstSharedPtr route,
                                           const std::string& filter_name,
                                           EnforcementMode mode) const {
  if (!route || !route->routeEntry()) {
    return engine(mode);
  }

  const auto* entry = route->routeEntry();
  const auto* route_local =
      entry->perFilterConfigTyped<RoleBasedAccessControlRouteSpecificFilterConfig>(filter_name)
          ?: entry->virtualHost()
                 .perFilterConfigTyped<RoleBasedAccessControlRouteSpecificFilterConfig>(
                     filter_name);

  if (route_local) {
    return route_local->engine(mode);
  }

  return engine(mode);
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
