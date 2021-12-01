#include "source/extensions/filters/http/rbac/rbac_filter.h"

#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/http/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::extensions::filters::http::rbac::v3::RBAC& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : stats_(Filters::Common::RBAC::generateStats(stats_prefix,
                                                  proto_config.shadow_rules_stat_prefix(), scope)),
      shadow_rules_stat_prefix_(proto_config.shadow_rules_stat_prefix()),
      engine_(Filters::Common::RBAC::createEngine(proto_config, validation_visitor)),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(proto_config, validation_visitor)) {}

const Filters::Common::RBAC::RoleBasedAccessControlEngineImpl*
RoleBasedAccessControlFilterConfig::engine(const Router::RouteConstSharedPtr route,
                                           Filters::Common::RBAC::EnforcementMode mode) const {
  const auto* route_local = Http::Utility::resolveMostSpecificPerFilterConfig<
      RoleBasedAccessControlRouteSpecificFilterConfig>("envoy.filters.http.rbac", route);

  if (route_local) {
    return route_local->engine(mode);
  }

  return engine(mode);
}

RoleBasedAccessControlRouteSpecificFilterConfig::RoleBasedAccessControlRouteSpecificFilterConfig(
    const envoy::extensions::filters::http::rbac::v3::RBACPerRoute& per_route_config,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  // Moved from member initializer to ctor body to overcome clang false warning about memory
  // leak (clang-analyzer-cplusplus.NewDeleteLeaks,-warnings-as-errors).
  // Potentially https://lists.llvm.org/pipermail/llvm-bugs/2018-July/066769.html
  engine_ = Filters::Common::RBAC::createEngine(per_route_config.rbac(), validation_visitor);
  shadow_engine_ =
      Filters::Common::RBAC::createShadowEngine(per_route_config.rbac(), validation_visitor);
}

Http::FilterHeadersStatus
RoleBasedAccessControlFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  ENVOY_LOG(
      debug,
      "checking request: requestedServerName: {}, sourceIP: {}, directRemoteIP: {}, remoteIP: {},"
      "localAddress: {}, ssl: {}, headers: {}, dynamicMetadata: {}",
      callbacks_->connection()->requestedServerName(),
      callbacks_->connection()->connectionInfoProvider().remoteAddress()->asString(),
      callbacks_->streamInfo().downstreamAddressProvider().directRemoteAddress()->asString(),
      callbacks_->streamInfo().downstreamAddressProvider().remoteAddress()->asString(),
      callbacks_->streamInfo().downstreamAddressProvider().localAddress()->asString(),
      callbacks_->connection()->ssl()
          ? "uriSanPeerCertificate: " +
                absl::StrJoin(callbacks_->connection()->ssl()->uriSanPeerCertificate(), ",") +
                ", dnsSanPeerCertificate: " +
                absl::StrJoin(callbacks_->connection()->ssl()->dnsSansPeerCertificate(), ",") +
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
    if (shadow_engine->handleAction(*callbacks_->connection(), headers, callbacks_->streamInfo(),
                                    &effective_policy_id)) {
      ENVOY_LOG(debug, "shadow allowed, matched policy {}",
                effective_policy_id.empty() ? "none" : effective_policy_id);
      config_->stats().shadow_allowed_.inc();
    } else {
      ENVOY_LOG(debug, "shadow denied, matched policy {}",
                effective_policy_id.empty() ? "none" : effective_policy_id);
      config_->stats().shadow_denied_.inc();
      shadow_resp_code =
          Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultDenied;
    }

    ProtobufWkt::Struct metrics;

    auto& fields = *metrics.mutable_fields();
    if (!effective_policy_id.empty()) {
      *fields[config_->shadowEffectivePolicyIdField()].mutable_string_value() = effective_policy_id;
    }

    *fields[config_->shadowEngineResultField()].mutable_string_value() = shadow_resp_code;
    callbacks_->streamInfo().setDynamicMetadata("envoy.filters.http.rbac", metrics);
  }

  const auto engine =
      config_->engine(callbacks_->route(), Filters::Common::RBAC::EnforcementMode::Enforced);
  if (engine != nullptr) {
    std::string effective_policy_id;
    bool allowed = engine->handleAction(*callbacks_->connection(), headers,
                                        callbacks_->streamInfo(), &effective_policy_id);
    const std::string log_policy_id = effective_policy_id.empty() ? "none" : effective_policy_id;
    if (allowed) {
      ENVOY_LOG(debug, "enforced allowed, matched policy {}", log_policy_id);
      config_->stats().allowed_.inc();
      return Http::FilterHeadersStatus::Continue;
    } else {
      ENVOY_LOG(debug, "enforced denied, matched policy {}", log_policy_id);
      callbacks_->sendLocalReply(Http::Code::Forbidden, "RBAC: access denied", nullptr,
                                 absl::nullopt,
                                 Filters::Common::RBAC::responseDetail(log_policy_id));
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
