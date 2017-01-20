#pragma once

#include "server/configuration_impl.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the client SSL auth filter. @see NetworkFilterConfigFactory.
 */
class ClientSslAuthConfigFactory : public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                const Json::Object& json_config,
                                                Server::Instance& server);

private:
  static const std::string CLIENT_SSL_SCHEMA;
};

} // Configuration
} // Server
