#include <string>

#include "envoy/network/connection.h"

#include "common/filter/echo.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the echo filter. @see NamedNetworkFilterConfigFactory.
 */
class EchoConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(NetworkFilterType type, const Json::Object&,
                                             Server::Instance&) override {
    if (type != NetworkFilterType::Read) {
      throw EnvoyException(
          fmt::format("{} network filter must be configured as a read filter.", name()));
    }

    return [](Network::FilterManager& filter_manager)
        -> void { filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new Filter::Echo()}); };
  }

  std::string name() override { return "echo"; }
};

/**
 * Static registration for the echo filter. @see RegisterNamedNetworkFilterConfigFactory.
 */
static RegisterNamedNetworkFilterConfigFactory<EchoConfigFactory> registered_;

} // Configuration
} // Server
} // Envoy
