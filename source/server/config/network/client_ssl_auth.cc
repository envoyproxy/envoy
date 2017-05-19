#include "server/config/network/client_ssl_auth.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/filter/auth/client_ssl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb ClientSslAuthConfigFactory::createFilterFactory(
    NetworkFilterType type, const Json::Object& json_config, Server::Instance& server) {
  if (type != NetworkFilterType::Read) {
    throw EnvoyException(
        fmt::format("{} network filter must be configured as a read filter.", name()));
  }

  Filter::Auth::ClientSsl::ConfigSharedPtr config(Filter::Auth::ClientSsl::Config::create(
      json_config, server.threadLocal(), server.clusterManager(), server.dispatcher(),
      server.stats(), server.random()));
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        Network::ReadFilterSharedPtr{new Filter::Auth::ClientSsl::Instance(config)});
  };
}

std::string ClientSslAuthConfigFactory::name() { return "client_ssl_auth"; }

/**
 * Static registration for the client SSL auth filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<ClientSslAuthConfigFactory> registered_;

} // Configuration
} // Server
} // Envoy
