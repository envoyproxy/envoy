#include "extensions/filters/http/local_ratelimit/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

Http::FilterFactoryCb LocalRateLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  FilterConfigSharedPtr filter_config = std::make_shared<FilterConfig>(
      proto_config, context.dispatcher(), context.scope(), context.runtime());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
LocalRateLimitFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const FilterConfig>(proto_config, context.dispatcher(), context.scope(),
                                              context.runtime(), true);
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(LocalRateLimitFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.local_rate_limit"};

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
