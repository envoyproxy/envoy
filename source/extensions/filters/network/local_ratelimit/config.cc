#include "extensions/filters/network/local_ratelimit/config.h"

#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.validate.h"

#include "extensions/filters/network/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

Network::FilterFactoryCb LocalRateLimitConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& proto_config,
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
