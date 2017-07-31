#include "server/config/network/ratelimit.h"

#include <chrono>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/filter/ratelimit.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb RateLimitConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                                   FactoryContext& context) {
  RateLimit::TcpFilter::ConfigSharedPtr config(
      new RateLimit::TcpFilter::Config(json_config, context.scope(), context.runtime()));
  const uint32_t timeout_ms = json_config.getInteger("timeout_ms", 20);
  return [config, timeout_ms, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new RateLimit::TcpFilter::Instance(
        config, context.rateLimitClient(std::chrono::milliseconds(timeout_ms)))});
  };
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RateLimitConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
