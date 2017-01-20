#include "client_ssl_auth.h"

#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/filter/auth/client_ssl.h"

namespace Server {
namespace Configuration {

const std::string ClientSslAuthConfigFactory::CLIENT_SSL_SCHEMA(
    "{\n"
    "\t\"$schema\": \"http://json-schema.org/schema#\", \n"
    "\t\"properties\":{\n"
    "\t\t\"auth_api_cluster\" : { \"type\" : \"string\" },\n"
    "\t\t\"stat_prefix\" : {\"type\" : \"string\"},\n"
    "\t\t\"ip_white_list\" : {\"type\" : \"array\", \"items\" : { \"type\": \"string\", \"format\" "
    ": \"ipv4\" } }\n"
    "\t},\n"
    "\t\"required\": [\"auth_api_cluster\", \"stat_prefix\"],\n"
    "\t\"additionalProperties\": false\n"
    "}");

NetworkFilterFactoryCb
ClientSslAuthConfigFactory::tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                   const Json::Object& json_config,
                                                   Server::Instance& server) {
  if (type != NetworkFilterType::Read || name != "client_ssl_auth") {
    return nullptr;
  }

  json_config.validateSchema(CLIENT_SSL_SCHEMA);

  Filter::Auth::ClientSsl::ConfigPtr config(Filter::Auth::ClientSsl::Config::create(
      json_config, server.threadLocal(), server.clusterManager(), server.dispatcher(),
      server.stats(), server.random()));
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        Network::ReadFilterPtr{new Filter::Auth::ClientSsl::Instance(config)});
  };
}

/**
 * Static registration for the client SSL auth filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<ClientSslAuthConfigFactory> registered_;

} // Configuration
} // Server
