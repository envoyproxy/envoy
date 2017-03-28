#include "server/config/network/client_ssl_auth.h"

#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/filter/auth/client_ssl.h"

namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
ClientSslAuthConfigFactory::tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                   const Json::Object& json_config,
                                                   Server::Instance& server) {
  if (type != NetworkFilterType::Read || name != "client_ssl_auth") {
    return nullptr;
  }

  Filter::Auth::ClientSsl::ConfigSharedPtr config(Filter::Auth::ClientSsl::Config::create(
      json_config, server.threadLocal(), server.clusterManager(), server.dispatcher(),
      server.stats(), server.random()));
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        Network::ReadFilterSharedPtr{new Filter::Auth::ClientSsl::Instance(config)});
  };
}

/**
 * Static registration for the client SSL auth filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<ClientSslAuthConfigFactory> registered_;

} // Configuration
} // Server
