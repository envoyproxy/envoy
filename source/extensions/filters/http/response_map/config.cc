#include "extensions/filters/http/response_map/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/response_map/response_map_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ResponseMapFilter {

Http::FilterFactoryCb ResponseMapFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::response_map::v3::ResponseMap& proto_config,
    const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  ResponseMapFilterConfigSharedPtr config =
      std::make_shared<ResponseMapFilterConfig>(proto_config, stats_prefix, context);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<ResponseMapFilter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
ResponseMapFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::response_map::v3::ResponseMapPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validationVisitor) {
  return std::make_shared<FilterConfigPerRoute>(proto_config, context, validationVisitor);
}

/**
 * Static registration for the response_map filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ResponseMapFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.response_map"};

} // namespace ResponseMapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
