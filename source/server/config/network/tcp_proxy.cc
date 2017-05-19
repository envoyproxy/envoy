#include "server/config/network/tcp_proxy.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/filter/tcp_proxy.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb TcpProxyConfigFactory::createFilterFactory(NetworkFilterType type,
                                                                  const Json::Object& config,
                                                                  Server::Instance& server) {
  if (type != NetworkFilterType::Read) {
    throw EnvoyException(
        fmt::format("{} network filter must be configured as a read filter.", name()));
  }

  Filter::TcpProxyConfigSharedPtr filter_config(
      new Filter::TcpProxyConfig(config, server.clusterManager(), server.stats()));
  return [filter_config, &server](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        Network::ReadFilterSharedPtr{new Filter::TcpProxy(filter_config, server.clusterManager())});
  };
}

std::string TcpProxyConfigFactory::name() { return "tcp_proxy"; }

/**
 * Static registration for the tcp_proxy filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<TcpProxyConfigFactory> registered_;

} // Configuration
} // Server
} // Envoy
