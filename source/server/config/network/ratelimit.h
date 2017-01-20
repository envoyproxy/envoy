#pragma once

#include "server/configuration_impl.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the rate limit filter. @see NetworkFilterConfigFactory.
 */
class RateLimitConfigFactory : public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                const Json::Object& json_config,
                                                Server::Instance& server);

private:
  static const std::string RATELIMIT_SCHEMA;
};

} // Configuration
} // Server
