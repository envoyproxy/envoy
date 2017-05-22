#pragma once

#include <string>

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the redis proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class RedisProxyFilterConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(NetworkFilterType type, const Json::Object& config,
                                             Server::Instance& server) override;
  std::string name() override;
};

} // Configuration
} // Server
} // Envoy
