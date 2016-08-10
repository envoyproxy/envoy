#include "envoy/network/connection.h"

#include "common/filter/echo.h"
#include "server/configuration_impl.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the echo filter. @see NetworkFilterConfigFactory.
 */
class EchoConfigFactory : public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                const Json::Object&, Server::Instance&) {
    if (type != NetworkFilterType::Read || name != "echo") {
      return nullptr;
    }

    return [](Network::Connection& connection)
        -> void { connection.addReadFilter(Network::ReadFilterPtr{new Filter::Echo()}); };
  }
};

/**
 * Static registration for the echo filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<EchoConfigFactory> registered_;

} // Configuration
} // Server
