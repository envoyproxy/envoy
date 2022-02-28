#include "source/extensions/filters/http/gcp_authn/filter_config.h"

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {

using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;
using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterPerRouteConfig;

Http::FilterFactoryCb GcpAuthnFilterFactory::createFilterFactoryFromProtoTyped(
    const GcpAuthnFilterConfig& config, const std::string&, FactoryContext& context) {
  return [config, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<GcpAuthnFilter>(config, context));
  };
}

// Router::RouteSpecificFilterConfigConstSharedPtr
// GcpAuthnFilterFactory::createRouteSpecificFilterConfigTyped(
//     const GcpAuthnFilterPerRouteConfig& config,
//     Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
//   return std::make_shared<RouteSpecificGcpAuthnFilterConfig>(config);
// }

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GcpAuthnFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
