#include "source/extensions/filters/http/cors/config.h"

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/extensions/filters/http/cors/cors_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

using CorsPolicyImpl =
    Router::CorsPolicyImplBase<envoy::extensions::filters::http::cors::v3::CorsPolicy>;

Http::FilterFactoryCb CorsFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cors::v3::Cors&, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  CorsFilterConfigSharedPtr config =
      std::make_shared<CorsFilterConfig>(stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CorsFilter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
CorsFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::cors::v3::CorsPolicy& policy,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<CorsPolicyImpl>(policy, context.runtime());
}

/**
 * Static registration for the cors filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CorsFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.cors"};

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
