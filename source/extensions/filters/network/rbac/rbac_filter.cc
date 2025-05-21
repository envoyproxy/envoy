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
    const Matcher::DataInputFactory<Http::HttpMatchingData>&, absl::string_view type_url) {
  static const absl::flat_hash_set<std::string> allowed_inputs_set{
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
              ->full_name())},
      {TypeUtil::descriptorFullNameToTypeUrl(
          envoy::extensions::matching::common_inputs::network::v3::FilterStateInput::descriptor()
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
    : stats_(Filters::Common::RBAC::generateStats(proto_config.stat_prefix(), "",
                                                  proto_config.shadow_rules_stat_prefix(), scope)),
      shadow_rules_stat_prefix_(proto_config.shadow_rules_stat_prefix()),
      engine_(Filters::Common::RBAC::createEngine(proto_config, context, validation_visitor,
                                                  action_validation_visitor_)),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(
          proto_config, context, validation_visitor, action_validation_visitor_)),
      enforcement_type_(proto_config.enforcement_type()),
      delay_deny_ms_(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, delay_deny, 0)) {}

Network::FilterStatus RoleBasedAccessControlFilter::onData(Buffer::Instance&, bool) {
  if (is_delay_denied_) {
    return Network::FilterStatus::StopIteration;
  }

  if (ENVOY_LOG_CHECK_LEVEL(debug)) {
    const auto& connection = callbacks_->connection();
    const auto& stream_info = connection.streamInfo();
    const auto& downstream_addr_provider = stream_info.downstreamAddressProvider();

    std::string ssl_info;
    if (connection.ssl()) {
      ssl_info = "uriSanPeerCertificate: " +
                 absl::StrJoin(connection.ssl()->uriSanPeerCertificate(), ",") +
                 ", dnsSanPeerCertificate: " +
                 absl::StrJoin(connection.ssl()->dnsSansPeerCertificate(), ",") +
                 ", subjectPeerCertificate: " + connection.ssl()->subjectPeerCertificate();
    } else {
      ssl_info = "none";
    }

    ENVOY_LOG(debug,
              "checking connection: requestedServerName: {}, sourceIP: {}, directRemoteIP: {},"
              "remoteIP: {}, localAddress: {}, ssl: {}, dynamicMetadata: {}",
              connection.requestedServerName(),
              connection.connectionInfoProvider().remoteAddress()->asString(),
              downstream_addr_provider.directRemoteAddress()->asString(),
              downstream_addr_provider.remoteAddress()->asString(),
              downstream_addr_provider.localAddress()->asString(), ssl_info,
              stream_info.dynamicMetadata().DebugString());
  }

  std::string log_policy_id = "none";
  // When the enforcement type is continuous always do the RBAC checks. If it is a one-time check,
  // run the check once and skip it for later onData calls.
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

    if (const std::chrono::milliseconds duration = config_->delayDenyMs();
        duration > std::chrono::milliseconds(0)) {
      ENVOY_LOG(debug, "connection will be delay denied in {}ms", duration.count());
      delay_timer_ = callbacks_->connection().dispatcher().createTimer(
          [this]() -> void { closeConnection(); });
      ASSERT(!is_delay_denied_);
      is_delay_denied_ = true;
      callbacks_->connection().readDisable(true);
      delay_timer_->enableTimer(duration);
    } else {
      closeConnection();
    }
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_LOG(debug, "no engine, allowed by default");
  return Network::FilterStatus::Continue;
}

void RoleBasedAccessControlFilter::closeConnection() const {
  callbacks_->connection().close(Network::ConnectionCloseType::NoFlush, "rbac_deny_close");
}

void RoleBasedAccessControlFilter::resetTimerState() {
  if (delay_timer_) {
    delay_timer_->disableTimer();
    delay_timer_.reset();
  }
}

void RoleBasedAccessControlFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    resetTimerState();
  }
}

void RoleBasedAccessControlFilter::setDynamicMetadata(const std::string& shadow_engine_result,
                                                      const std::string& shadow_policy_id) const {
  ProtobufWkt::Struct metrics;
  auto& fields = *metrics.mutable_fields();
  if (!shadow_policy_id.empty()) {
    fields[config_->shadowEffectivePolicyIdField()].set_string_value(shadow_policy_id);
  }
  fields[config_->shadowEngineResultField()].set_string_value(shadow_engine_result);
  callbacks_->connection().streamInfo().setDynamicMetadata(NetworkFilterNames::get().Rbac, metrics);
}

Result
RoleBasedAccessControlFilter::checkEngine(Filters::Common::RBAC::EnforcementMode mode) const {
  const auto engine = config_->engine(mode);
  if (engine == nullptr) {
    return Result{None, "none"};
  }

  // Check authorization decision and do Action operations
  std::string effective_policy_id;
  bool allowed = engine->handleAction(callbacks_->connection(),
                                      callbacks_->connection().streamInfo(), &effective_policy_id);

  const std::string log_policy_id = effective_policy_id.empty() ? "none" : effective_policy_id;
  const bool is_shadow = (mode == Filters::Common::RBAC::EnforcementMode::Shadow);

  if (allowed) {
    if (is_shadow) {
      ENVOY_LOG(debug, "shadow allowed, matched policy {}", log_policy_id);
      config_->stats().shadow_allowed_.inc();
      setDynamicMetadata(
          Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultAllowed,
          effective_policy_id);
    } else {
      ENVOY_LOG(debug, "enforced allowed, matched policy {}", log_policy_id);
      config_->stats().allowed_.inc();
    }
    return Result{Allow, effective_policy_id};
  } else {
    if (is_shadow) {
      ENVOY_LOG(debug, "shadow denied, matched policy {}", log_policy_id);
      config_->stats().shadow_denied_.inc();
      setDynamicMetadata(
          Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultDenied,
          effective_policy_id);
    } else {
      ENVOY_LOG(debug, "enforced denied, matched policy {}", log_policy_id);
      config_->stats().denied_.inc();
    }
    return Result{Deny, log_policy_id};
  }
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
