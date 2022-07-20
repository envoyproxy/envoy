#include "source/extensions/filters/http/rbac/config.h"

#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/rbac/rbac_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

Http::FilterFactoryCb RoleBasedAccessControlFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::rbac::v3::RBAC& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& base_context) {
  Server::Configuration::ServerFactoryContext& context = base_context.getServerFactoryContext();
  auto config = std::make_shared<RoleBasedAccessControlFilterConfig>(
      proto_config, stats_prefix, base_context.scope(), context,
      context.messageValidationVisitor());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<RoleBasedAccessControlFilter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
RoleBasedAccessControlFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::rbac::v3::RBACPerRoute& proto_config,
    Server::Configuration::FactoryContext& base_context,
    ProtobufMessage::ValidationVisitor& validator) {
  return std::make_shared<const RoleBasedAccessControlRouteSpecificFilterConfig>(
      proto_config, base_context.getServerFactoryContext(), validator);
}

/**
 * Static registration for the RBAC filter. @see RegisterFactory
 */
REGISTER_FACTORY(RoleBasedAccessControlFilterConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
