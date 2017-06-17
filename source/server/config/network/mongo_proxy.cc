#include "server/config/network/mongo_proxy.h"

#include <string>

#include "envoy/network/connection.h"

#include "common/json/config_schemas.h"
#include "common/mongo/proxy.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
MongoProxyFilterConfigFactory::createFilterFactory(const Json::Object& config,
                                                   FactoryContext& context) {
  config.validateSchema(Json::Schema::MONGO_PROXY_NETWORK_FILTER_SCHEMA);

  std::string stat_prefix = "mongo." + config.getString("stat_prefix") + ".";
  Mongo::AccessLogSharedPtr access_log;
  if (config.hasObject("access_log")) {
    access_log.reset(
        new Mongo::AccessLog(config.getString("access_log"), context.accessLogManager()));
  }

  return [stat_prefix, &context, access_log](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(Network::FilterSharedPtr{
        new Mongo::ProdProxyFilter(stat_prefix, context.scope(), context.runtime(), access_log)});
  };
}

/**
 * Static registration for the mongo filter. @see RegisterNamedNetworkFilterConfigFactory.
 */
static RegisterNamedNetworkFilterConfigFactory<MongoProxyFilterConfigFactory> registered_;

} // Configuration
} // Server
} // Envoy
