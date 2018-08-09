#include "extensions/filters/common/rbac/config.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

RoleBasedAccessControlRouteSpecificFilterConfig::RoleBasedAccessControlRouteSpecificFilterConfig(
    const envoy::config::filter::http::rbac::v2::RBACPerRoute& per_route_config)
    : engine_(createEngine(per_route_config.rbac())),
      shadow_engine_(createShadowEngine(per_route_config.rbac())) {}

RoleBasedAccessControlFilterStats
RoleBasedAccessControlFilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "rbac.";
  return {ALL_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope)
    : stats_(generateStats(stats_prefix, scope)), engine_(createEngine(proto_config)),
      shadow_engine_(createShadowEngine(proto_config)) {}

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::config::filter::network::rbac::v2::RBAC& proto_config, Stats::Scope& scope)
    : stats_(generateStats(proto_config.stat_prefix(), scope)),
      engine_(createEngine(proto_config, true /* disable_http_rules */)),
      shadow_engine_(createShadowEngine(proto_config, true /* disable_http_rules */)) {}

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
