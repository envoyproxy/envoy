#include "source/extensions/filters/http/rbac/rbac_filter.h"

#include "envoy/stats/scope.h"

#include "source/common/http/matching/inputs.h"
#include "source/common/http/utility.h"
#include "source/common/network/matching/inputs.h"
#include "source/common/ssl/matching/inputs.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

absl::Status ActionValidationVisitor::performDataInputValidation(
    const Envoy::Matcher::DataInputFactory<Http::HttpMatchingData>&, absl::string_view type_url) {
  static absl::flat_hash_set<std::string> allowed_inputs_set{
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::extensions::matching::common_inputs::network::v3::DestinationIPInput::descriptor()
              ->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(envoy::extensions::matching::common_inputs::network::
                                                 v3::DestinationPortInput::descriptor()
                                                     ->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::extensions::matching::common_inputs::network::v3::SourceIPInput::descriptor()
              ->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::extensions::matching::common_inputs::network::v3::SourcePortInput::descriptor()
              ->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::extensions::matching::common_inputs::network::v3::DirectSourceIPInput::descriptor()
              ->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::extensions::matching::common_inputs::network::v3::ServerNameInput::descriptor()
              ->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::type::matcher::v3::HttpRequestHeaderMatchInput::descriptor()->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::extensions::matching::common_inputs::ssl::v3::UriSanInput::descriptor()
              ->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::extensions::matching::common_inputs::ssl::v3::DnsSanInput::descriptor()
              ->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::extensions::matching::common_inputs::ssl::v3::SubjectInput::descriptor()
              ->full_name())}};
  if (allowed_inputs_set.contains(type_url)) {
    return absl::OkStatus();
  }

  return absl::InvalidArgumentError(fmt::format("RBAC HTTP filter cannot match on '{}'", type_url));
}

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::extensions::filters::http::rbac::v3::RBAC& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : stats_(Filters::Common::RBAC::generateStats(stats_prefix,
                                                  proto_config.shadow_rules_stat_prefix(), scope)),
      shadow_rules_stat_prefix_(proto_config.shadow_rules_stat_prefix()),
      engine_(Filters::Common::RBAC::createEngine(proto_config, context, validation_visitor,
                                                  action_validation_visitor_)),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(
          proto_config, context, validation_visitor, action_validation_visitor_)) {}

const Filters::Common::RBAC::RoleBasedAccessControlEngine*
RoleBasedAccessControlFilterConfig::engine(const Http::StreamFilterCallbacks* callbacks,
                                           Filters::Common::RBAC::EnforcementMode mode) const {
  const auto* route_local = Http::Utility::resolveMostSpecificPerFilterConfig<
      RoleBasedAccessControlRouteSpecificFilterConfig>(callbacks);

  if (route_local) {
    return route_local->engine(mode);
  }

  return engine(mode);
}

RoleBasedAccessControlRouteSpecificFilterConfig::RoleBasedAccessControlRouteSpecificFilterConfig(
    const envoy::extensions::filters::http::rbac::v3::RBACPerRoute& per_route_config,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  // Moved from member initializer to ctor body to overcome clang false warning about memory
  // leak (clang-analyzer-cplusplus.NewDeleteLeaks,-warnings-as-errors).
  // Potentially https://lists.llvm.org/pipermail/llvm-bugs/2018-July/066769.html
  engine_ = Filters::Common::RBAC::createEngine(per_route_config.rbac(), context,
                                                validation_visitor, action_validation_visitor_);
  shadow_engine_ = Filters::Common::RBAC::createShadowEngine(
      per_route_config.rbac(), context, validation_visitor, action_validation_visitor_);
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
      config_->engine(callbacks_, Filters::Common::RBAC::EnforcementMode::Shadow);

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

  const auto engine = config_->engine(callbacks_, Filters::Common::RBAC::EnforcementMode::Enforced);
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
