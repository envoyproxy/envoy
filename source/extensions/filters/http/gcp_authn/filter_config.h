#pragma once

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

class GcpAuthnFilterFactory
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>,
      public Logger::Loggable<Logger::Id::filter> {
public:
  GcpAuthnFilterFactory() : UnifiedFactoryBase(std::string(FilterName)) {}

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

private:
  // Shared factory creation used by the listener/cluster and route/vhost-level paths.
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactory(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context,
      Stats::Scope& scope);
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
