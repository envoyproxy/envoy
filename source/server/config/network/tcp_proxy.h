#pragma once

#include "server/configuration_impl.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the tcp proxy filter. @see NetworkFilterConfigFactory.
 */
class TcpProxyConfigFactory : public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                const Json::Object& config,
                                                Server::Instance& server);

private:
  static const std::string TCP_PROXY_SCHEMA;
};

} // Configuration
} // Server
