#pragma once

#include <string>

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the client SSL auth filter. @see NamedNetworkFilterConfigFactory.
 */
class ClientSslAuthConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(NetworkFilterType type,
                                             const Json::Object& json_config,
                                             Server::Instance& server) override;
  std::string name() override;
};

} // Configuration
} // Server
} // Envoy
