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
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(proto_config)),
      expr_(proto_config.has_condition() ? Filters::Common::Expr::create(proto_config.condition()) : nullptr) {}

const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
RoleBasedAccessControlFilterConfig::engine(const Router::RouteConstSharedPtr route,
                                           Filters::Common::RBAC::EnforcementMode mode) const {
  if (!route || !route->routeEntry()) {
    return engine(mode);
  }

  const std::string& name = HttpFilterNames::get().Rbac;
  const auto* entry = route->routeEntry();
  const auto* tmp =
      entry->perFilterConfigTyped<RoleBasedAccessControlRouteSpecificFilterConfig>(name);
  const auto* route_local =
      tmp ? tmp
          : entry->virtualHost()
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
  const auto& shadow_engine =
      config_->engine(callbacks_->route(), Filters::Common::RBAC::EnforcementMode::Shadow);

  if (shadow_engine.has_value()) {
    std::string shadow_resp_code =
        Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultAllowed;
    if (shadow_engine->allowed(*callbacks_->connection(), headers,
                               callbacks_->streamInfo().dynamicMetadata(), &effective_policy_id)) {
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

  const auto& engine =
      config_->engine(callbacks_->route(), Filters::Common::RBAC::EnforcementMode::Enforced);
  if (engine.has_value()) {
    if (engine->allowed(*callbacks_->connection(), headers,
                        callbacks_->streamInfo().dynamicMetadata(), nullptr)) {
      ENVOY_LOG(debug, "enforced allowed");
      config_->stats().allowed_.inc();
    } else {
      ENVOY_LOG(debug, "enforced denied");
      callbacks_->sendLocalReply(Http::Code::Forbidden, "RBAC: access denied", nullptr,
                                 absl::nullopt, RcDetails::get().RbacAccessDenied);
      config_->stats().denied_.inc();
      return Http::FilterHeadersStatus::StopIteration;
    }
  } else {
    ENVOY_LOG(debug, "no engine, allowed by default");
  }

  if (expr_ != nullptr) {
    auto eval_status = Filter::Common::Expr::Evaluate(expr_, headers, callbacks_->streamInfo());
    if (!eval_status.has_value()) {
      ENVOY_LOG(debug, "evaluation failed");
      return Http::FilterHeadersStatus::StopIteration;
    }
    auto result = eval_status.value();
    ENVOY_LOG(trace, "value: {}", result.IsError() ? result.ErrorOrDie()->message() : 
        (result.IsString() ? result.StringOrDie().value() : ""));
    if (! result.IsBool() || (result.IsBool() && !result.BoolOrDie())) {
      ENVOY_LOG(debug, "evaluated to non-bool or false");
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
