#include "extensions/filters/http/rbac/rbac_filter.h"

#include "common/http/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope)
    : stats_(RoleBasedAccessControlFilter::generateStats(stats_prefix, scope)),
      engine_(proto_config.has_rules()
                  ? absl::make_optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>(
                        proto_config.rules())
                  : absl::nullopt),
      shadow_engine_(
          proto_config.has_shadow_rules()
              ? absl::make_optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>(
                    proto_config.shadow_rules())
              : absl::nullopt) {}

RoleBasedAccessControlFilterStats
RoleBasedAccessControlFilter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "rbac.";
  return {ALL_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
RoleBasedAccessControlFilterConfig::engine(const Router::RouteConstSharedPtr route,
                                           EnforcementMode mode) const {
  if (!route || !route->routeEntry()) {
    return engine(mode);
  }

  const std::string& name = HttpFilterNames::get().Rbac;
  const auto* entry = route->routeEntry();

  const auto* route_local =
      entry->perFilterConfigTyped<RoleBasedAccessControlRouteSpecificFilterConfig>(name)
          ?: entry->virtualHost()
                 .perFilterConfigTyped<RoleBasedAccessControlRouteSpecificFilterConfig>(name);

  if (route_local) {
    return route_local->engine(mode);
  }

  return engine(mode);
}

RoleBasedAccessControlRouteSpecificFilterConfig::RoleBasedAccessControlRouteSpecificFilterConfig(
    const envoy::config::filter::http::rbac::v2::RBACPerRoute& per_route_config)
    : engine_(per_route_config.rbac().has_rules()
                  ? absl::make_optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>(
                        per_route_config.rbac().rules())
                  : absl::nullopt),
      shadow_engine_(
          per_route_config.rbac().has_shadow_rules()
              ? absl::make_optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>(
                    per_route_config.rbac().shadow_rules())
              : absl::nullopt) {}

Http::FilterHeadersStatus RoleBasedAccessControlFilter::decodeHeaders(Http::HeaderMap& headers,
                                                                      bool) {
  ENVOY_LOG(
      debug,
      "checking request: remoteAddress: {}, localAddress: {}, ssl: {}, headers: {}, "
      "dynamicMetadata: {}",
      callbacks_->connection()->remoteAddress()->asString(),
      callbacks_->connection()->localAddress()->asString(),
      callbacks_->connection()->ssl()
          ? "uriSanPeerCertificate: " + callbacks_->connection()->ssl()->uriSanPeerCertificate() +
                ", subjectPeerCertificate: " +
                callbacks_->connection()->ssl()->subjectPeerCertificate()
          : "none",
      headers, callbacks_->requestInfo().dynamicMetadata().DebugString());
  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>& shadow_engine =
      config_->engine(callbacks_->route(), EnforcementMode::Shadow);
  if (shadow_engine.has_value()) {
    if (shadow_engine->allowed(*callbacks_->connection(), headers,
                               callbacks_->requestInfo().dynamicMetadata())) {
      ENVOY_LOG(debug, "shadow allowed");
      config_->stats().shadow_allowed_.inc();
    } else {
      ENVOY_LOG(debug, "shadow denied");
      config_->stats().shadow_denied_.inc();
    }
  }

  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>& engine =
      config_->engine(callbacks_->route(), EnforcementMode::Enforced);
  if (engine.has_value()) {
    if (engine->allowed(*callbacks_->connection(), headers,
                        callbacks_->requestInfo().dynamicMetadata())) {
      ENVOY_LOG(debug, "enforced allowed");
      config_->stats().allowed_.inc();
      return Http::FilterHeadersStatus::Continue;
    } else {
      ENVOY_LOG(debug, "enforced denied");
      callbacks_->sendLocalReply(Http::Code::Forbidden, "RBAC: access denied", nullptr);
      config_->stats().denied_.inc();
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  ENVOY_LOG(debug, "no engine, allowed by default");
  return Http::FilterHeadersStatus::Continue;
}

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
