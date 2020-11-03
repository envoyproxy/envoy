#include "extensions/filters/http/bandwidth_limit/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

Http::FilterFactoryCb BandwidthLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  FilterConfigSharedPtr filter_config = std::make_shared<FilterConfig>(
      proto_config, context.dispatcher(), context.scope(), context.runtime());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
BandwidthLimitFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const FilterConfig>(proto_config, context.dispatcher(), context.scope(),
                                              context.runtime(), true);
}

/**
 * Static registration for the bandwidth limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(BandwidthLimitFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.bandwidth_limit"};

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
