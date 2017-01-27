#include "ratelimit.h"

#include "envoy/network/connection.h"

#include "common/filter/ratelimit.h"

namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
RateLimitConfigFactory::tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                               const Json::Object& json_config,
                                               Server::Instance& server) {
  if (type != NetworkFilterType::Read || name != "ratelimit") {
    return nullptr;
  }

  RateLimit::TcpFilter::ConfigPtr config(
      new RateLimit::TcpFilter::Config(json_config, server.stats(), server.runtime()));
  return [config, &server](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterPtr{new RateLimit::TcpFilter::Instance(
        config, server.rateLimitClient(Optional<std::chrono::milliseconds>()))});
  };
}

/**
 * Static registration for the rate limit filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<RateLimitConfigFactory> registered_;

} // Configuration
} // Server
