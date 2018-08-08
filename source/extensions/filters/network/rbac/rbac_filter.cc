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

  const Envoy::Http::HeaderMapImpl empty_header;
  const envoy::api::v2::core::Metadata empty_metadata;
  const std::string& filter_name = NetworkFilterNames::get().Rbac;
  const auto& shadow_engine =
      config_->engine(nullptr, filter_name, Filters::Common::RBAC::EnforcementMode::Shadow);
  if (shadow_engine.has_value()) {
    // TODO(quanlin): Support metric collection for RBAC network filter.
    if (shadow_engine->allowed(callbacks_->connection(), empty_header, empty_metadata, nullptr)) {
      ENVOY_LOG(debug, "shadow allowed");
      config_->stats().shadow_allowed_.inc();
    } else {
      ENVOY_LOG(debug, "shadow denied");
      config_->stats().shadow_denied_.inc();
    }
  }

  const auto& engine =
      config_->engine(nullptr, filter_name, Filters::Common::RBAC::EnforcementMode::Enforced);
  if (engine.has_value()) {
    if (engine->allowed(callbacks_->connection(), empty_header, empty_metadata, nullptr)) {
      ENVOY_LOG(debug, "enforced allowed");
      config_->stats().allowed_.inc();
      return Network::FilterStatus::Continue;
    } else {
      ENVOY_LOG(debug, "enforced denied");
      config_->stats().denied_.inc();
      callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      return Network::FilterStatus::StopIteration;
    }
  }

  ENVOY_LOG(debug, "no engine, allowed by default");
  return Network::FilterStatus::Continue;
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
