#pragma once

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"
#include "envoy/config/filter/http/rbac/v2/rbac.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

/**
 * Config registration for the RBAC filter. @see NamedHttpFilterConfigFactory.
 */
class RoleBasedAccessControlFilterConfigFactory
    : public Common::FactoryBase<envoy::config::filter::http::rbac::v2::RBAC,
                                 envoy::config::filter::http::rbac::v2::RBACPerRoute> {
public:
  RoleBasedAccessControlFilterConfigFactory() : FactoryBase(HttpFilterNames::get().Rbac) {}

private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::config::filter::http::rbac::v2::RBACPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
