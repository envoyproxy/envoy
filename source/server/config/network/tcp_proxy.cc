#include "server/config/network/tcp_proxy.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/filter/tcp_proxy.h"

namespace Lyft {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb TcpProxyConfigFactory::tryCreateFilterFactory(NetworkFilterType type,
                                                                     const std::string& name,
                                                                     const Json::Object& config,
                                                                     Server::Instance& server) {
  if (type != NetworkFilterType::Read || name != "tcp_proxy") {
    return nullptr;
  }

  Filter::TcpProxyConfigSharedPtr filter_config(
      new Filter::TcpProxyConfig(config, server.clusterManager(), server.stats()));
  return [filter_config, &server](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        Network::ReadFilterSharedPtr{new Filter::TcpProxy(filter_config, server.clusterManager())});
  };
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<TcpProxyConfigFactory> registered_;

} // Configuration
} // Server
} // Lyft