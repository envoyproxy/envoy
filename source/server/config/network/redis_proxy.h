#pragma once

#include <string>

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the redis proxy filter. @see NetworkFilterConfigFactory.
 */
class RedisProxyFilterConfigFactory : public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(NetworkFilterType type, const Json::Object& config,
                                             Server::Instance& server) override;
  std::string name() override;
};

} // Configuration
} // Server
} // Envoy
