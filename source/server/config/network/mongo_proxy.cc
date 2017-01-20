#include "mongo_proxy.h"

#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/json/json_loader.h"
#include "common/mongo/proxy.h"

namespace Server {
namespace Configuration {

const std::string MongoProxyFilterConfigFactory::MONGO_PROXY_SCHEMA(
    "{\n"
    "\t\"$schema\": \"http://json-schema.org/schema#\", \n"
    "\t\"properties\":{\n"
    "\t\t\"stat_prefix\" : {\"type\" : \"string\"},\n"
    "\t\t\"access_log\" : {\"type\" : \"string\"}\n"
    "\t},\n"
    "\t\"required\": [\"stat_prefix\"],\n"
    "\t\"additionalProperties\": false\n"
    "}");

NetworkFilterFactoryCb MongoProxyFilterConfigFactory::tryCreateFilterFactory(
    NetworkFilterType type, const std::string& name, const Json::Object& config,
    Server::Instance& server) {
  if (type != NetworkFilterType::Both || name != "mongo_proxy") {
    return nullptr;
  }

  config.validateSchema(MONGO_PROXY_SCHEMA);

  std::string stat_prefix = "mongo." + config.getString("stat_prefix") + ".";
  Mongo::AccessLogPtr access_log;
  if (config.hasObject("access_log")) {
    access_log.reset(
        new Mongo::AccessLog(config.getString("access_log"), server.accessLogManager()));
  }

  return [stat_prefix, &server, access_log](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(Network::FilterPtr{
        new Mongo::ProdProxyFilter(stat_prefix, server.stats(), server.runtime(), access_log)});
  };
}

/**
 * Static registration for the mongo filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<MongoProxyFilterConfigFactory> registered_;

} // Configuration
} // Server
