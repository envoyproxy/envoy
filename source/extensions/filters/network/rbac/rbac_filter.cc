#include "extensions/filters/network/rbac/rbac_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/network/connection.h"

#include "extensions/filters/network/well_known_names.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::extensions::filters::network::rbac::v3::RBAC& proto_config, Stats::Scope& scope)
    : stats_(Filters::Common::RBAC::generateStats(proto_config.stat_prefix(), scope)),
      engine_(Filters::Common::RBAC::createEngine(proto_config)),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(proto_config)),
      enforcement_type_(proto_config.enforcement_type()) {}

Network::FilterStatus RoleBasedAccessControlFilter::onData(Buffer::Instance&, bool) {
  ENVOY_LOG(debug,
            "checking connection: requestedServerName: {}, sourceIP: {}, directRemoteIP: {},"
            "remoteIP: {}, localAddress: {}, ssl: {}, dynamicMetadata: {}",
            callbacks_->connection().requestedServerName(),
            callbacks_->connection().remoteAddress()->asString(),
            callbacks_->connection().streamInfo().downstreamDirectRemoteAddress()->asString(),
            callbacks_->connection().streamInfo().downstreamRemoteAddress()->asString(),
            callbacks_->connection().streamInfo().downstreamLocalAddress()->asString(),
            callbacks_->connection().ssl()
                ? "uriSanPeerCertificate: " +
                      absl::StrJoin(callbacks_->connection().ssl()->uriSanPeerCertificate(), ",") +
                      ", dnsSanPeerCertificate: " +
                      absl::StrJoin(callbacks_->connection().ssl()->dnsSansPeerCertificate(), ",") +
                      ", subjectPeerCertificate: " +
                      callbacks_->connection().ssl()->subjectPeerCertificate()
                : "none",
            callbacks_->connection().streamInfo().dynamicMetadata().DebugString());

  // When the enforcement type is continuous always do the RBAC checks. If it is a one time check,
  // run the check once and skip it for subsequent onData calls.
  if (config_->enforcementType() ==
      envoy::extensions::filters::network::rbac::v3::RBAC::CONTINUOUS) {
    shadow_engine_result_ = checkEngine(Filters::Common::RBAC::EnforcementMode::Shadow);
    engine_result_ = checkEngine(Filters::Common::RBAC::EnforcementMode::Enforced);
  } else {
    if (shadow_engine_result_ == Unknown) {
      // TODO(quanlin): Pass the shadow engine results to other filters.
      shadow_engine_result_ = checkEngine(Filters::Common::RBAC::EnforcementMode::Shadow);
    }

    if (engine_result_ == Unknown) {
      engine_result_ = checkEngine(Filters::Common::RBAC::EnforcementMode::Enforced);
    }
  }

  if (engine_result_ == Allow) {
    return Network::FilterStatus::Continue;
  } else if (engine_result_ == Deny) {
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
    *fields[Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().ShadowEffectivePolicyIdField]
         .mutable_string_value() = shadow_policy_id;
  }
  *fields[Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().ShadowEngineResultField]
       .mutable_string_value() = shadow_engine_result;
  callbacks_->connection().streamInfo().setDynamicMetadata(NetworkFilterNames::get().Rbac, metrics);
}

EngineResult
RoleBasedAccessControlFilter::checkEngine(Filters::Common::RBAC::EnforcementMode mode) {
  const auto engine = config_->engine(mode);
  if (engine != nullptr) {
    std::string effective_policy_id;

    // Check authorization decision and do Action operations
    if (engine->handleAction(callbacks_->connection(), callbacks_->connection().streamInfo(),
                             &effective_policy_id)) {
      if (mode == Filters::Common::RBAC::EnforcementMode::Shadow) {
        ENVOY_LOG(debug, "shadow allowed, matched policy {}",
                  effective_policy_id.empty() ? "none" : effective_policy_id);
        config_->stats().shadow_allowed_.inc();
        setDynamicMetadata(
            Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultAllowed,
            effective_policy_id);
      } else if (mode == Filters::Common::RBAC::EnforcementMode::Enforced) {
        ENVOY_LOG(debug, "enforced allowed, matched policy {}",
                  effective_policy_id.empty() ? "none" : effective_policy_id);
        config_->stats().allowed_.inc();
      }
      return Allow;
    } else {
      if (mode == Filters::Common::RBAC::EnforcementMode::Shadow) {
        ENVOY_LOG(debug, "shadow denied, matched policy {}",
                  effective_policy_id.empty() ? "none" : effective_policy_id);
        config_->stats().shadow_denied_.inc();
        setDynamicMetadata(
            Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().EngineResultDenied,
            effective_policy_id);
      } else if (mode == Filters::Common::RBAC::EnforcementMode::Enforced) {
        ENVOY_LOG(debug, "enforced denied, matched policy {}",
                  effective_policy_id.empty() ? "none" : effective_policy_id);
        config_->stats().denied_.inc();
      }
      return Deny;
    }
  }
  return None;
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
