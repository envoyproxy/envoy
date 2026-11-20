#include "rbac_filter.h"

#include "envoy/stats/scope.h"

#include "source/common/http/matching/inputs.h"
#include "source/common/http/utility.h"
#include "source/common/network/matching/inputs.h"
#include "source/common/ssl/matching/inputs.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

// Define the static allowed_inputs_set
const absl::flat_hash_set<std::string> ActionValidationVisitor::allowed_inputs_set_{
    {TypeUtil::descriptorFullNameToTypeUrl(
        envoy::extensions::matching::common_inputs::network::v3::DestinationIPInput::descriptor()
            ->full_name())},
    {TypeUtil::descriptorFullNameToTypeUrl(
        envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput::descriptor()
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
        envoy::extensions::matching::common_inputs::network::v3::NetworkNamespaceInput::descriptor()
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
            ->full_name())},
    {TypeUtil::descriptorFullNameToTypeUrl(
        envoy::extensions::matching::common_inputs::network::v3::DynamicMetadataInput::descriptor()
            ->full_name())},
    {TypeUtil::descriptorFullNameToTypeUrl(
        xds::type::matcher::v3::HttpAttributesCelMatchInput::descriptor()->full_name())},
    {TypeUtil::descriptorFullNameToTypeUrl(
        envoy::extensions::matching::common_inputs::network::v3::FilterStateInput::descriptor()
            ->full_name())}};

absl::Status ActionValidationVisitor::performDataInputValidation(
    const Matcher::DataInputFactory<Http::HttpMatchingData>&, absl::string_view type_url) {
  if (allowed_inputs_set_.contains(type_url)) {
    return absl::OkStatus();
  }

  return absl::InvalidArgumentError(fmt::format("RBAC HTTP filter cannot match on '{}'", type_url));
}

namespace {

DynamicMetadataFieldNames fieldNamesFor(absl::string_view stat_prefix,
                                        Filters::Common::RBAC::EnforcementMode mode) {
  const auto& keys = Filters::Common::RBAC::DynamicMetadataKeysSingleton::get();
  if (mode == Filters::Common::RBAC::EnforcementMode::Shadow) {
    return {absl::StrCat(stat_prefix, keys.ShadowEffectivePolicyIdField),
            absl::StrCat(stat_prefix, keys.ShadowEngineResultField)};
  }
  return {absl::StrCat(stat_prefix, keys.EnforcedEffectivePolicyIdField),
          absl::StrCat(stat_prefix, keys.EnforcedEngineResultField)};
}

// A route config with an empty stat prefix keeps the filter config's field names, so it builds
// none of its own.
std::optional<DynamicMetadataFieldNames>
overridingFieldNamesFor(absl::string_view stat_prefix,
                        Filters::Common::RBAC::EnforcementMode mode) {
  if (stat_prefix.empty()) {
    return std::nullopt;
  }
  return fieldNamesFor(stat_prefix, mode);
}

} // namespace

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::extensions::filters::http::rbac::v3::RBAC& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : stats_(Filters::Common::RBAC::generateStats(stats_prefix, proto_config.rules_stat_prefix(),
                                                  proto_config.shadow_rules_stat_prefix(),
                                                  /*http_filter=*/true, scope)),
      enforced_field_names_(fieldNamesFor(proto_config.rules_stat_prefix(),
                                          Filters::Common::RBAC::EnforcementMode::Enforced)),
      shadow_field_names_(fieldNamesFor(proto_config.shadow_rules_stat_prefix(),
                                        Filters::Common::RBAC::EnforcementMode::Shadow)),
      per_rule_stats_(proto_config.track_per_rule_stats()),
      engine_(Filters::Common::RBAC::createEngine(proto_config, context, validation_visitor,
                                                  action_validation_visitor_)),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(
          proto_config, context, validation_visitor, action_validation_visitor_)) {}

const DynamicMetadataFieldNames& RoleBasedAccessControlFilterConfig::fieldNames(
    const RoleBasedAccessControlRouteSpecificFilterConfig* route_local,
    const Filters::Common::RBAC::EnforcementMode mode) const {
  if (route_local) {
    const auto& route_names = route_local->fieldNames(mode);
    if (route_names.has_value()) {
      return *route_names;
    }
  }

  return mode == Filters::Common::RBAC::EnforcementMode::Enforced ? enforced_field_names_
                                                                  : shadow_field_names_;
}

const Filters::Common::RBAC::RoleBasedAccessControlEngine*
RoleBasedAccessControlFilterConfig::engine(
    const RoleBasedAccessControlRouteSpecificFilterConfig* route_local,
    const Filters::Common::RBAC::EnforcementMode mode) const {
  if (route_local) {
    return route_local->engine(mode);
  }

  return engine(mode);
}

bool RoleBasedAccessControlFilterConfig::perRuleStatsEnabled(
    const RoleBasedAccessControlRouteSpecificFilterConfig* route_local) const {
  if (route_local) {
    return route_local->perRuleStatsEnabled();
  }

  return per_rule_stats_;
}

RoleBasedAccessControlRouteSpecificFilterConfig::RoleBasedAccessControlRouteSpecificFilterConfig(
    const envoy::extensions::filters::http::rbac::v3::RBACPerRoute& per_route_config,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : enforced_field_names_(
          overridingFieldNamesFor(per_route_config.rbac().rules_stat_prefix(),
                                  Filters::Common::RBAC::EnforcementMode::Enforced)),
      shadow_field_names_(
          overridingFieldNamesFor(per_route_config.rbac().shadow_rules_stat_prefix(),
                                  Filters::Common::RBAC::EnforcementMode::Shadow)),
      per_rule_stats_(per_route_config.rbac().track_per_rule_stats()) {
  // Moved from member initializer to ctor body to overcome clang false warning about memory
  // leak (clang-analyzer-cplusplus.NewDeleteLeaks,-warnings-as-errors).
  // Potentially https://lists.llvm.org/pipermail/llvm-bugs/2018-July/066769.html
  engine_ = Filters::Common::RBAC::createEngine(per_route_config.rbac(), context,
                                                validation_visitor, action_validation_visitor_);
  shadow_engine_ = Filters::Common::RBAC::createShadowEngine(
      per_route_config.rbac(), context, validation_visitor, action_validation_visitor_);
}

// Evaluates the shadow engine policy and updates stats accordingly
void RoleBasedAccessControlFilter::evaluateShadowEngine(
    const Http::RequestHeaderMap& headers,
    const RoleBasedAccessControlRouteSpecificFilterConfig* route_local,
    EngineResult& shadow) const {
  const auto shadow_engine =
      config_->engine(route_local, Filters::Common::RBAC::EnforcementMode::Shadow);
  if (shadow_engine == nullptr) {
    return;
  }

  std::string& effective_policy_id = shadow.effective_policy_id_;
  absl::string_view shadow_response_code =
      Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultAllowed;

  const bool per_rule_stats_enabled = config_->perRuleStatsEnabled(route_local);

  if (shadow_engine->handleAction(*callbacks_->connection(), headers, callbacks_->streamInfo(),
                                  &effective_policy_id)) {
    ENVOY_LOG(debug, "shadow allowed, matched policy {}",
              effective_policy_id.empty() ? "none" : effective_policy_id);
    config_->stats().shadow_allowed_.inc();
    if (!effective_policy_id.empty() && per_rule_stats_enabled) {
      config_->stats().incPolicyShadowAllowed(effective_policy_id);
    }
  } else {
    ENVOY_LOG(debug, "shadow denied, matched policy {}",
              effective_policy_id.empty() ? "none" : effective_policy_id);
    config_->stats().shadow_denied_.inc();
    if (!effective_policy_id.empty() && per_rule_stats_enabled) {
      config_->stats().incPolicyShadowDenied(effective_policy_id);
    }
    shadow_response_code =
        Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultDenied;
  }

  shadow.result_ = shadow_response_code;
}

// Evaluates the enforced engine policy and returns the appropriate filter status
Http::FilterHeadersStatus RoleBasedAccessControlFilter::evaluateEnforcedEngine(
    const Http::RequestHeaderMap& headers,
    const RoleBasedAccessControlRouteSpecificFilterConfig* route_local,
    EngineResult& enforced) const {
  const auto engine =
      config_->engine(route_local, Filters::Common::RBAC::EnforcementMode::Enforced);
  if (engine == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  std::string& effective_policy_id = enforced.effective_policy_id_;
  const bool per_rule_stats_enabled = config_->perRuleStatsEnabled(route_local);

  const bool allowed = engine->handleAction(*callbacks_->connection(), headers,
                                            callbacks_->streamInfo(), &effective_policy_id);

  absl::string_view log_policy_id = effective_policy_id;
  if (effective_policy_id.empty()) {
    log_policy_id = "none";
  }

  if (allowed) {
    ENVOY_LOG(debug, "enforced allowed, matched policy {}", log_policy_id);
    config_->stats().allowed_.inc();
    if (!effective_policy_id.empty() && per_rule_stats_enabled) {
      config_->stats().incPolicyAllowed(effective_policy_id);
    }

    enforced.result_ =
        Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultAllowed;

    return Http::FilterHeadersStatus::Continue;
  }

  ENVOY_LOG(debug, "enforced denied, matched policy {}", log_policy_id);
  callbacks_->sendLocalReply(Http::Code::Forbidden, "RBAC: access denied", nullptr, std::nullopt,
                             Filters::Common::RBAC::responseDetail(log_policy_id));
  config_->stats().denied_.inc();
  if (!effective_policy_id.empty() && per_rule_stats_enabled) {
    config_->stats().incPolicyDenied(effective_policy_id);
  }

  enforced.result_ = Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultDenied;

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus
RoleBasedAccessControlFilter::evaluateEngines(const Http::RequestHeaderMap& headers,
                                              absl::string_view metadata_namespace) const {
  // One lookup serves both engines and every field name. The connection manager keeps a cleared
  // route until the stream ends, so this pointer outlives sendLocalReply(), and a denial writes the
  // field names of the config whose engines ran.
  const auto* route_local = Http::Utility::resolveMostSpecificPerFilterConfig<
      RoleBasedAccessControlRouteSpecificFilterConfig>(callbacks_);

  EngineResult shadow;
  evaluateShadowEngine(headers, route_local, shadow);
  EngineResult enforced;
  const Http::FilterHeadersStatus status = evaluateEnforcedEngine(headers, route_local, enforced);

  // A config with neither engine writes no dynamic metadata.
  if (shadow.result_.empty() && enforced.result_.empty()) {
    return status;
  }

  // The fields are written after both engines ran and after any local reply returned, so nothing
  // sendLocalReply() runs sees them, such as the onLocalReply() callbacks, the local reply mapper,
  // the route's response header formatters and the encoder filters of a reply sent at once. One
  // namespace lookup serves every field, and each field replaces the value an earlier filter wrote
  // under the same name.
  auto& fields =
      *(*callbacks_->streamInfo().dynamicMetadata().mutable_filter_metadata())[metadata_namespace]
           .mutable_fields();
  const auto write = [&fields](EngineResult& result, const DynamicMetadataFieldNames& names) {
    if (result.result_.empty()) {
      return;
    }
    if (!result.effective_policy_id_.empty()) {
      fields[names.effective_policy_id_].set_string_value(std::move(result.effective_policy_id_));
    }
    fields[names.engine_result_].set_string_value(result.result_);
  };
  write(shadow, config_->fieldNames(route_local, Filters::Common::RBAC::EnforcementMode::Shadow));
  write(enforced,
        config_->fieldNames(route_local, Filters::Common::RBAC::EnforcementMode::Enforced));

  return status;
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

  const Http::FilterHeadersStatus status = evaluateEngines(headers, DynamicMetadataKey);
  if (status == Http::FilterHeadersStatus::StopIteration) {
    return status;
  }

  ENVOY_LOG(debug, "allowed by default or by policy");
  return Http::FilterHeadersStatus::Continue;
}

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
