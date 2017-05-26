#include "server/config/network/ratelimit.h"

#include <chrono>
#include <string>

#include "envoy/network/connection.h"

#include "common/filter/ratelimit.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb RateLimitConfigFactory::createFilterFactory(NetworkFilterType type,
                                                                   const Json::Object& json_config,
                                                                   Server::Instance& server) {
  if (type != NetworkFilterType::Read) {
    throw EnvoyException(
        fmt::format("{} network filter must be configured as a read filter.", name()));
  }

  RateLimit::TcpFilter::ConfigSharedPtr config(
      new RateLimit::TcpFilter::Config(json_config, server.stats(), server.runtime()));
  return [config, &server](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new RateLimit::TcpFilter::Instance(
        config, server.rateLimitClient(Optional<std::chrono::milliseconds>()))});
  };
}

std::string RateLimitConfigFactory::name() { return "ratelimit"; }

/**
 * Static registration for the rate limit filter. @see RegisterNamedNetworkFilterConfigFactory.
 */
static RegisterNamedNetworkFilterConfigFactory<RateLimitConfigFactory> registered_;

} // Configuration
} // Server
} // Envoy
