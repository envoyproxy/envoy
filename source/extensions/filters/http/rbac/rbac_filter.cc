#include "extensions/filters/http/rbac/rbac_filter.h"

#include "common/http/utility.h"

#include "extensions/filters/common/rbac/engine_impl.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope)
    : stats_(RoleBasedAccessControlFilter::generateStats(stats_prefix, scope)),
      engine_(proto_config, false) {}

RoleBasedAccessControlFilterStats
RoleBasedAccessControlFilter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "rbac.";
  return {ALL_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

const Filters::Common::RBAC::RoleBasedAccessControlEngine&
RoleBasedAccessControlFilterConfig::engine(const Router::RouteConstSharedPtr route) const {
  if (!route || !route->routeEntry()) {
    return engine_;
  }

  const std::string& name = HttpFilterNames::get().RBAC;
  const auto* entry = route->routeEntry();

  const auto* route_local =
      entry->perFilterConfigTyped<Filters::Common::RBAC::RoleBasedAccessControlEngine>(name)
          ?: entry->virtualHost()
                 .perFilterConfigTyped<Filters::Common::RBAC::RoleBasedAccessControlEngine>(name);

  return route_local ? *route_local : engine_;
}

Http::FilterHeadersStatus RoleBasedAccessControlFilter::decodeHeaders(Http::HeaderMap& headers,
                                                                      bool) {
  const Filters::Common::RBAC::RoleBasedAccessControlEngine& engine =
      config_->engine(callbacks_->route());

  if (engine.allowed(*callbacks_->connection(), headers)) {
    config_->stats().allowed_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  callbacks_->sendLocalReply(Http::Code::Forbidden, "RBAC: access denied", nullptr);
  config_->stats().denied_.inc();
  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
