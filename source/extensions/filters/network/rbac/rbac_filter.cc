#include "extensions/filters/network/rbac/rbac_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/http/header_map_impl.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

Network::FilterStatus RoleBasedAccessControlFilter::onData(Buffer::Instance&, bool) {
  ENVOY_LOG(
      debug, "checking connection: remoteAddress: {}, localAddress: {}, ssl: {}",
      callbacks_->connection().remoteAddress()->asString(),
      callbacks_->connection().localAddress()->asString(),
      callbacks_->connection().ssl()
          ? "uriSanPeerCertificate: " + callbacks_->connection().ssl()->uriSanPeerCertificate() +
                ", subjectPeerCertificate: " +
                callbacks_->connection().ssl()->subjectPeerCertificate()
          : "none");

  if (shadow_engine_result_ == UNKNOWN) {
    // TODO(quanlin): Support metric collection for RBAC network filter.
    // Only check the engine and increase stats for the first time call to onData(), any following
    // calls to onData() could just use the cached result and no need to increase the stats anymore.
    shadow_engine_result_ = checkEngine(Filters::Common::RBAC::EnforcementMode::Shadow);
  }

  if (engine_result_ == UNKNOWN) {
    engine_result_ = checkEngine(Filters::Common::RBAC::EnforcementMode::Enforced);
  }

  if (engine_result_ == ALLOW) {
    return Network::FilterStatus::Continue;
  } else if (engine_result_ == DENY) {
    callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_LOG(debug, "no engine, allowed by default");
  return Network::FilterStatus::Continue;
}

EngineResult
RoleBasedAccessControlFilter::checkEngine(Filters::Common::RBAC::EnforcementMode mode) {
  const auto& engine = config_->engine(nullptr, NetworkFilterNames::get().Rbac, mode);
  if (engine.has_value()) {
    if (engine->allowed(callbacks_->connection(), Envoy::Http::HeaderMapImpl(),
                        envoy::api::v2::core::Metadata(), nullptr)) {
      if (mode == Filters::Common::RBAC::EnforcementMode::Shadow) {
        ENVOY_LOG(debug, "shadow allowed");
        config_->stats().shadow_allowed_.inc();
      } else if (mode == Filters::Common::RBAC::EnforcementMode::Enforced) {
        ENVOY_LOG(debug, "enforced allowed");
        config_->stats().allowed_.inc();
      }
      return ALLOW;
    } else {
      if (mode == Filters::Common::RBAC::EnforcementMode::Shadow) {
        ENVOY_LOG(debug, "shadow denied");
        config_->stats().shadow_denied_.inc();
      } else if (mode == Filters::Common::RBAC::EnforcementMode::Enforced) {
        ENVOY_LOG(debug, "enforced denied");
        config_->stats().denied_.inc();
      }
      return DENY;
    }
  }
  return NONE;
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
