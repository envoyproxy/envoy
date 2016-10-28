#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/filter/auth/client_ssl.h"
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
                                                Server::Instance& server) {
    if (type != NetworkFilterType::Read || name != "client_ssl_auth") {
      return nullptr;
    }

    Filter::Auth::ClientSsl::ConfigPtr config(new Filter::Auth::ClientSsl::Config(
        json_config, server.threadLocal(), server.clusterManager(), server.dispatcher(),
        server.stats(), server.runtime()));
    return [config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(
          Network::ReadFilterPtr{new Filter::Auth::ClientSsl::Instance(config)});
    };
  }
};

/**
 * Static registration for the client SSL auth filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<ClientSslAuthConfigFactory> registered_;

} // Configuration
} // Server
