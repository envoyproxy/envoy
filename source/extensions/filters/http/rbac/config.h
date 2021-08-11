#pragma once

#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

/**
 * Config registration for the RBAC filter. @see NamedHttpFilterConfigFactory.
 */
class RoleBasedAccessControlFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::rbac::v3::RBAC,
                                 envoy::extensions::filters::http::rbac::v3::RBACPerRoute> {
public:
  RoleBasedAccessControlFilterConfigFactory() : FactoryBase("envoy.filters.http.rbac") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::rbac::v3::RBAC& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::rbac::v3::RBACPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
