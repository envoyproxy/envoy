#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/filter/tcp_proxy.h"
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
                                                Server::Instance& server) {
    if (type != NetworkFilterType::Read || name != "tcp_proxy") {
      return nullptr;
    }

    Filter::TcpProxyConfigPtr filter_config(
        new Filter::TcpProxyConfig(config, server.clusterManager(), server.stats()));
    return [filter_config, &server](Network::Connection& connection) -> void {
      connection.addReadFilter(
          Network::ReadFilterPtr{new Filter::TcpProxy(filter_config, server.clusterManager())});
    };
  }
};

/**
 * Static registration for the tcp_proxy filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<TcpProxyConfigFactory> registered_;

} // Configuration
} // Server
