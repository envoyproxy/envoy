#include "extensions/filters/http/rbac/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/rbac/rbac_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

Http::FilterFactoryCb RoleBasedAccessControlFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  auto config = std::make_shared<RoleBasedAccessControlFilterConfig>(proto_config, stats_prefix,
                                                                     context.scope());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<RoleBasedAccessControlFilter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
RoleBasedAccessControlFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::config::filter::http::rbac::v2::RBACPerRoute& proto_config,
    Server::Configuration::FactoryContext&) {
  return std::make_shared<const RoleBasedAccessControlRouteSpecificFilterConfig>(proto_config);
}

/**
 * Static registration for the RBAC filter. @see RegisterFactory
 */
static Registry::RegisterFactory<RoleBasedAccessControlFilterConfigFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
