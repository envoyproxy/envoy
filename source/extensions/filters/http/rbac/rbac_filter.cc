#include "extensions/filters/http/rbac/rbac_filter.h"

#include "envoy/stats/scope.h"

#include "common/http/utility.h"

#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

struct RcDetailsValues {
  // The rbac filter rejected the request
  const std::string RbacAccessDenied = "rbac_access_denied";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope)
    : stats_(Filters::Common::RBAC::generateStats(stats_prefix, scope)),
      engine_(Filters::Common::RBAC::createEngine(proto_config)),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(proto_config)) {}

const Filters::Common::RBAC::RoleBasedAccessControlEngineImpl*
RoleBasedAccessControlFilterConfig::engine(const Router::RouteConstSharedPtr route,
                                           Filters::Common::RBAC::EnforcementMode mode) const {
  if (!route || !route->routeEntry()) {
    return engine(mode);
  }

  const std::string& name = HttpFilterNames::get().Rbac;
  const auto* entry = route->routeEntry();
  const auto* route_local =
      entry->mostSpecificPerFilterConfigTyped<RoleBasedAccessControlRouteSpecificFilterConfig>(
          name);

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
  ENVOY_LOG(debug,
            "checking request: remoteAddress: {}, localAddress: {}, ssl: {}, headers: {}, "
            "dynamicMetadata: {}",
            callbacks_->connection()->remoteAddress()->asString(),
            callbacks_->connection()->localAddress()->asString(),
            callbacks_->connection()->ssl()
                ? "uriSanPeerCertificate: " +
                      absl::StrJoin(callbacks_->connection()->ssl()->uriSanPeerCertificate(), ",") +
                      ", subjectPeerCertificate: " +
                      callbacks_->connection()->ssl()->subjectPeerCertificate()
                : "none",
            headers, callbacks_->streamInfo().dynamicMetadata().DebugString());

  std::string effective_policy_id;
  const auto shadow_engine =
      config_->engine(callbacks_->route(), Filters::Common::RBAC::EnforcementMode::Shadow);

  if (shadow_engine != nullptr) {
    std::string shadow_resp_code =
        Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultAllowed;
    // Refresh headers byte size before checking if allowed.
    // TODO(asraa): Remove this when entries in HeaderMap can no longer be modified by reference and
    // HeaderMap holds an accurate internal byte size count.
    headers.refreshByteSize();
    if (shadow_engine->allowed(*callbacks_->connection(), headers, callbacks_->streamInfo(),
                               &effective_policy_id)) {
      ENVOY_LOG(debug, "shadow allowed");
      config_->stats().shadow_allowed_.inc();
    } else {
      ENVOY_LOG(debug, "shadow denied");
      config_->stats().shadow_denied_.inc();
      shadow_resp_code =
          Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultDenied;
    }

    ProtobufWkt::Struct metrics;

    auto& fields = *metrics.mutable_fields();
    if (!effective_policy_id.empty()) {
      *fields[Filters::Common::RBAC::DynamicMetadataKeysSingleton::get()
                  .ShadowEffectivePolicyIdField]
           .mutable_string_value() = effective_policy_id;
    }

    *fields[Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().ShadowEngineResultField]
         .mutable_string_value() = shadow_resp_code;

    callbacks_->streamInfo().setDynamicMetadata(HttpFilterNames::get().Rbac, metrics);
  }

  const auto engine =
      config_->engine(callbacks_->route(), Filters::Common::RBAC::EnforcementMode::Enforced);
  if (engine != nullptr) {
    // Refresh headers byte size before checking if allowed.
    // TODO(asraa): Remove this when entries in HeaderMap can no longer be modified by reference and
    // HeaderMap holds an accurate internal byte size count.
    headers.refreshByteSize();
    if (engine->allowed(*callbacks_->connection(), headers, callbacks_->streamInfo(), nullptr)) {
      ENVOY_LOG(debug, "enforced allowed");
      config_->stats().allowed_.inc();
      return Http::FilterHeadersStatus::Continue;
    } else {
      ENVOY_LOG(debug, "enforced denied");
      callbacks_->sendLocalReply(Http::Code::Forbidden, "RBAC: access denied", nullptr,
                                 absl::nullopt, RcDetails::get().RbacAccessDenied);
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
