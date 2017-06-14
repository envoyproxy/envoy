#include <string>

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
  NetworkFilterFactoryCb createFilterFactory(const Json::Object&, FactoryContext&) override {
    return [](Network::FilterManager& filter_manager)
        -> void { filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new Filter::Echo()}); };
  }

  std::string name() override { return "echo"; }
  NetworkFilterType type() override { return NetworkFilterType::Read; }
};

/**
 * Static registration for the echo filter. @see RegisterNamedNetworkFilterConfigFactory.
 */
static RegisterNamedNetworkFilterConfigFactory<EchoConfigFactory> registered_;

} // Configuration
} // Server
} // Envoy
