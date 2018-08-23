#include "extensions/filters/http/rbac/rbac_filter.h"

#include "envoy/stats/scope.h"

#include "common/http/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

static const std::string resp_code_200 = "200";
static const std::string resp_code_403 = "403";
static const std::string shadow_policy_id_field = "shadow_effective_policyID";
static const std::string shadow_resp_code_field = "shadow_response_code";

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope)
    : stats_(Filters::Common::RBAC::generateStats(stats_prefix, scope)),
      engine_(Filters::Common::RBAC::createEngine(proto_config)),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(proto_config)) {}

const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
RoleBasedAccessControlFilterConfig::engine(const Router::RouteConstSharedPtr route,
                                           Filters::Common::RBAC::EnforcementMode mode) const {
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
    : engine_(Filters::Common::RBAC::createEngine(per_route_config.rbac())),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(per_route_config.rbac())) {}

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

  std::string effective_policy_id;
  const auto& shadow_engine =
      config_->engine(callbacks_->route(), Filters::Common::RBAC::EnforcementMode::Shadow);

  if (shadow_engine.has_value()) {
    std::string shadow_resp_code = resp_code_200;
    if (shadow_engine->allowed(*callbacks_->connection(), headers,
                               callbacks_->requestInfo().dynamicMetadata(), &effective_policy_id)) {
      ENVOY_LOG(debug, "shadow allowed");
      config_->stats().shadow_allowed_.inc();
    } else {
      ENVOY_LOG(debug, "shadow denied");
      config_->stats().shadow_denied_.inc();
      shadow_resp_code = resp_code_403;
    }

    ProtobufWkt::Struct metrics;

    auto& fields = *metrics.mutable_fields();
    if (!effective_policy_id.empty()) {
      *fields[shadow_policy_id_field].mutable_string_value() = effective_policy_id;
    }

    *fields[shadow_resp_code_field].mutable_string_value() = shadow_resp_code;

    callbacks_->requestInfo().setDynamicMetadata(HttpFilterNames::get().Rbac, metrics);
  }

  const auto& engine =
      config_->engine(callbacks_->route(), Filters::Common::RBAC::EnforcementMode::Enforced);
  if (engine.has_value()) {
    if (engine->allowed(*callbacks_->connection(), headers,
                        callbacks_->requestInfo().dynamicMetadata(), nullptr)) {
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
