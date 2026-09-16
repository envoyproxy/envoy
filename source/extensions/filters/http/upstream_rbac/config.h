#pragma once

#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace UpstreamRBACFilter {

/**
 * Config registration for the upstream RBAC filter. This filter is only usable as an upstream HTTP
 * filter, since it acts from the onHostSelected() callback. It reuses the existing
 * `envoy.extensions.filters.http.rbac.v3.RBAC` configuration. @see UpstreamHttpFilterConfigFactory.
 */
class UpstreamRoleBasedAccessControlFilterConfigFactory
    : public Common::UnifiedFactoryBase<envoy::extensions::filters::http::rbac::v3::RBAC> {
public:
  UpstreamRoleBasedAccessControlFilterConfigFactory()
      : UnifiedFactoryBase("envoy.filters.http.upstream_rbac") {}

  std::string category() const override { return "envoy.filters.http.upstream"; }

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::rbac::v3::RBAC& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(UpstreamRoleBasedAccessControlFilterConfigFactory);

} // namespace UpstreamRBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
