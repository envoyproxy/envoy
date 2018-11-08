#include "extensions/filters/network/rbac/rbac_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

class DynamicMetadataKeys {
public:
  const std::string EngineResultAllowed{"allowed"};
  const std::string EngineResultDenied{"denied"};
  const std::string ShadowEngineResultField{"shadow_engine_result"};
};

typedef ConstSingleton<DynamicMetadataKeys> DynamicMetadataKeysSingleton;

RoleBasedAccessControlFilterConfig::RoleBasedAccessControlFilterConfig(
    const envoy::config::filter::network::rbac::v2::RBAC& proto_config, Stats::Scope& scope)
    : stats_(Filters::Common::RBAC::generateStats(proto_config.stat_prefix(), scope)),
      engine_(Filters::Common::RBAC::createEngine(proto_config)),
      shadow_engine_(Filters::Common::RBAC::createShadowEngine(proto_config)) {}

Network::FilterStatus RoleBasedAccessControlFilter::onData(Buffer::Instance&, bool) {
  ENVOY_LOG(
      debug,
      "checking connection: requestedServerName: {}, remoteAddress: {}, localAddress: {}, ssl: {}, "
      "dynamicMetadata: {}",
      callbacks_->connection().requestedServerName(),
      callbacks_->connection().remoteAddress()->asString(),
      callbacks_->connection().localAddress()->asString(),
      callbacks_->connection().ssl()
          ? "uriSanPeerCertificate: " + callbacks_->connection().ssl()->uriSanPeerCertificate() +
                ", subjectPeerCertificate: " +
                callbacks_->connection().ssl()->subjectPeerCertificate()
          : "none",
      callbacks_->connection().streamInfo().dynamicMetadata().DebugString());

  if (shadow_engine_result_ == Unknown) {
    // TODO(quanlin): Pass the shadow engine results to other filters.
    // Only check the engine and increase stats for the first time call to onData(), any following
    // calls to onData() could just use the cached result and no need to increase the stats anymore.
    shadow_engine_result_ = checkEngine(Filters::Common::RBAC::EnforcementMode::Shadow);
  }

  if (engine_result_ == Unknown) {
    engine_result_ = checkEngine(Filters::Common::RBAC::EnforcementMode::Enforced);
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
    *fields[Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().ShadowPolicyIdField]
         .mutable_string_value() = shadow_policy_id;
  }
  *fields[DynamicMetadataKeysSingleton::get().ShadowEngineResultField].mutable_string_value() =
      shadow_engine_result;
  callbacks_->connection().streamInfo().setDynamicMetadata(NetworkFilterNames::get().Rbac, metrics);
}

EngineResult
RoleBasedAccessControlFilter::checkEngine(Filters::Common::RBAC::EnforcementMode mode) {
  const auto& engine = config_->engine(mode);
  if (engine.has_value()) {
    std::string effective_policy_id;
    if (engine->allowed(callbacks_->connection(),
                        callbacks_->connection().streamInfo().dynamicMetadata(),
                        &effective_policy_id)) {
      if (mode == Filters::Common::RBAC::EnforcementMode::Shadow) {
        ENVOY_LOG(debug, "shadow allowed");
        config_->stats().shadow_allowed_.inc();
        setDynamicMetadata(DynamicMetadataKeysSingleton::get().EngineResultAllowed,
                           effective_policy_id);
      } else if (mode == Filters::Common::RBAC::EnforcementMode::Enforced) {
        ENVOY_LOG(debug, "enforced allowed");
        config_->stats().allowed_.inc();
      }
      return Allow;
    } else {
      if (mode == Filters::Common::RBAC::EnforcementMode::Shadow) {
        ENVOY_LOG(debug, "shadow denied");
        config_->stats().shadow_denied_.inc();
        setDynamicMetadata(DynamicMetadataKeysSingleton::get().EngineResultDenied,
                           effective_policy_id);
      } else if (mode == Filters::Common::RBAC::EnforcementMode::Enforced) {
        ENVOY_LOG(debug, "enforced denied");
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
