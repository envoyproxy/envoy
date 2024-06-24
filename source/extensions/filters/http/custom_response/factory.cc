#include "source/extensions/filters/http/custom_response/factory.h"

#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

::Envoy::Http::FilterFactoryCb CustomResponseFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    const std::string& stats_prefix, Envoy::Server::Configuration::FactoryContext& context) {
  Stats::StatNameManagedStorage prefix(stats_prefix, context.scope().symbolTable());
  auto config_ptr =
      std::make_shared<FilterConfig>(config, context.serverFactoryContext(), prefix.statName());
  return [config_ptr](::Envoy::Http::FilterChainFactoryCallbacks& callbacks) mutable -> void {
    callbacks.addStreamFilter(std::make_shared<CustomResponseFilter>(config_ptr));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
CustomResponseFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    Envoy::Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfig>(config, context, context.scope().prefix());
}
/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CustomResponseFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
