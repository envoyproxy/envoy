#include "source/extensions/filters/http/bandwidth_limit/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

Http::FilterFactoryCb BandwidthLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    const std::string&, Server::Configuration::FactoryContext& base_context) {
  Server::Configuration::ServerFactoryContext& context = base_context.getServerFactoryContext();
  FilterConfigSharedPtr filter_config = std::make_shared<FilterConfig>(
      proto_config, base_context.scope(), context.runtime(), context.timeSource());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<BandwidthLimiter>(filter_config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
BandwidthLimitFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    Server::Configuration::FactoryContext& base_context, ProtobufMessage::ValidationVisitor&) {
  Server::Configuration::ServerFactoryContext& context = base_context.getServerFactoryContext();
  return std::make_shared<const FilterConfig>(proto_config, base_context.scope(), context.runtime(),
                                              context.timeSource(), true);
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
