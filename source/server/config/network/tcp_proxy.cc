#include "tcp_proxy.h"

#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/filter/tcp_proxy.h"

namespace Server {
namespace Configuration {

NetworkFilterFactoryCb TcpProxyConfigFactory::tryCreateFilterFactory(NetworkFilterType type,
                                                                     const std::string& name,
                                                                     const Json::Object& config,
                                                                     Server::Instance& server) {
  if (type != NetworkFilterType::Read || name != "tcp_proxy") {
    return nullptr;
  }

  Filter::TcpProxyConfigPtr filter_config(
      new Filter::TcpProxyConfig(config, server.clusterManager(), server.stats()));
  return [filter_config, &server](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        Network::ReadFilterPtr{new Filter::TcpProxy(filter_config, server.clusterManager())});
  };
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<TcpProxyConfigFactory> registered_;

} // Configuration
} // Server
