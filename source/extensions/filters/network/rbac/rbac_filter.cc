#include "source/extensions/filters/network/rbac/rbac_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/network/connection.h"

#include "source/common/network/matching/inputs.h"
#include "source/common/ssl/matching/inputs.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
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

  return absl::InvalidArgumentError(fmt::format("RBAC network filter cannot match '{}'", type_url));
}

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::extensions::filters::network::rbac::v3::RBAC& proto_config, Stats::Scope& scope,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : stats_(Filters::Common::RBAC::generateStats(proto_config.stat_prefix(),
                                                  proto_config.shadow_rules_stat_prefix(), scope)),
      shadow_rules_stat_prefix_(proto_config.shadow_rules_stat_prefix()),
      engine_(Filters::Common::RBAC::createEngine(proto_config, context, validation_visitor,
                                                  action_validation_visitor_)),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(
          proto_config, context, validation_visitor, action_validation_visitor_)),
      enforcement_type_(proto_config.enforcement_type()) {}

Network::FilterStatus RoleBasedAccessControlFilter::onData(Buffer::Instance&, bool) {
  ENVOY_LOG(
      debug,
      "checking connection: requestedServerName: {}, sourceIP: {}, directRemoteIP: {},"
      "remoteIP: {}, localAddress: {}, ssl: {}, dynamicMetadata: {}",
      callbacks_->connection().requestedServerName(),
      callbacks_->connection().connectionInfoProvider().remoteAddress()->asString(),
      callbacks_->connection()
          .streamInfo()
          .downstreamAddressProvider()
          .directRemoteAddress()
          ->asString(),
      callbacks_->connection().streamInfo().downstreamAddressProvider().remoteAddress()->asString(),
      callbacks_->connection().streamInfo().downstreamAddressProvider().localAddress()->asString(),
      callbacks_->connection().ssl()
          ? "uriSanPeerCertificate: " +
                absl::StrJoin(callbacks_->connection().ssl()->uriSanPeerCertificate(), ",") +
                ", dnsSanPeerCertificate: " +
                absl::StrJoin(callbacks_->connection().ssl()->dnsSansPeerCertificate(), ",") +
                ", subjectPeerCertificate: " +
                callbacks_->connection().ssl()->subjectPeerCertificate()
          : "none",
      callbacks_->connection().streamInfo().dynamicMetadata().DebugString());

  std::string log_policy_id = "none";
  // When the enforcement type is continuous always do the RBAC checks. If it is a one time check,
  // run the check once and skip it for subsequent onData calls.
  if (config_->enforcementType() ==
      envoy::extensions::filters::network::rbac::v3::RBAC::CONTINUOUS) {
    shadow_engine_result_ =
        checkEngine(Filters::Common::RBAC::EnforcementMode::Shadow).engine_result_;
    auto result = checkEngine(Filters::Common::RBAC::EnforcementMode::Enforced);
    engine_result_ = result.engine_result_;
    log_policy_id = result.connection_termination_details_;
  } else {
    if (shadow_engine_result_ == Unknown) {
      // TODO(quanlin): Pass the shadow engine results to other filters.
      shadow_engine_result_ =
          checkEngine(Filters::Common::RBAC::EnforcementMode::Shadow).engine_result_;
    }

    if (engine_result_ == Unknown) {
      auto result = checkEngine(Filters::Common::RBAC::EnforcementMode::Enforced);
      engine_result_ = result.engine_result_;
      log_policy_id = result.connection_termination_details_;
    }
  }

  if (engine_result_ == Allow) {
    return Network::FilterStatus::Continue;
  } else if (engine_result_ == Deny) {
    callbacks_->connection().streamInfo().setConnectionTerminationDetails(
        Filters::Common::RBAC::responseDetail(log_policy_id));
    callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_LOG(debug, "no engine, allowed by default");
  return Network::FilterStatus::Continue;
}

void RoleBasedAccessControlFilter::setDynamicMetadata(std::string shadow_engine_result,
                                                      std::string shadow_policy_id) {
  ProtobufWkt::Struct metrics;
  auto& fields = *metrics.mutable_fields();
  if (!shadow_policy_id.empty()) {
    *fields[config_->shadowEffectivePolicyIdField()].mutable_string_value() = shadow_policy_id;
  }
  *fields[config_->shadowEngineResultField()].mutable_string_value() = shadow_engine_result;
  callbacks_->connection().streamInfo().setDynamicMetadata(NetworkFilterNames::get().Rbac, metrics);
}

Result RoleBasedAccessControlFilter::checkEngine(Filters::Common::RBAC::EnforcementMode mode) {
  const auto engine = config_->engine(mode);
  std::string effective_policy_id;
  if (engine != nullptr) {
    // Check authorization decision and do Action operations
    bool allowed = engine->handleAction(
        callbacks_->connection(), callbacks_->connection().streamInfo(), &effective_policy_id);
    const std::string log_policy_id = effective_policy_id.empty() ? "none" : effective_policy_id;
    if (allowed) {
      if (mode == Filters::Common::RBAC::EnforcementMode::Shadow) {
        ENVOY_LOG(debug, "shadow allowed, matched policy {}", log_policy_id);
        config_->stats().shadow_allowed_.inc();
        setDynamicMetadata(
            Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultAllowed,
            effective_policy_id);
      } else if (mode == Filters::Common::RBAC::EnforcementMode::Enforced) {
        ENVOY_LOG(debug, "enforced allowed, matched policy {}", log_policy_id);
        config_->stats().allowed_.inc();
      }
      return Result{Allow, effective_policy_id};
    } else {
      if (mode == Filters::Common::RBAC::EnforcementMode::Shadow) {
        ENVOY_LOG(debug, "shadow denied, matched policy {}", log_policy_id);
        config_->stats().shadow_denied_.inc();
        setDynamicMetadata(
            Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultDenied,
            effective_policy_id);
      } else if (mode == Filters::Common::RBAC::EnforcementMode::Enforced) {
        ENVOY_LOG(debug, "enforced denied, matched policy {}", log_policy_id);
        config_->stats().denied_.inc();
      }
      return Result{Deny, log_policy_id};
    }
  }
  return Result{None, "none"};
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
