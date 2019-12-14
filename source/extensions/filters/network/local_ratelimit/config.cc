#include "extensions/filters/network/local_ratelimit/config.h"

#include "extensions/filters/network/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

Network::FilterFactoryCb LocalRateLimitConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::local_rate_limit::v2alpha::LocalRateLimit& proto_config,
    Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr filter_config(
      new Config(proto_config, context.dispatcher(), context.scope(), context.runtime()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(filter_config));
  };
}

/**
 * Static registration for the local rate limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(LocalRateLimitConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
