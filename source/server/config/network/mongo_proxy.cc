#include "server/config/network/mongo_proxy.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/json/config_schemas.h"
#include "common/mongo/proxy.h"

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

  Mongo::FaultConfigSharedPtr fault_config;
  if (config.hasObject("fault")) {
    fault_config = std::make_shared<Mongo::FaultConfig>(*config.getObject("fault"));
  }

  return [stat_prefix, &context, access_log,
          fault_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<Mongo::ProdProxyFilter>(
        stat_prefix, context.scope(), context.runtime(), access_log, fault_config));
  };
}

/**
 * Static registration for the mongo filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<MongoProxyFilterConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
