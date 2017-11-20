#include "server/config/network/mongo_proxy.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/mongo/proxy.h"

#include "fmt/format.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb MongoProxyFilterConfigFactory::createMongoProxyFactory(
    const envoy::api::v2::filter::network::MongoProxy& config, FactoryContext& context) {

  ASSERT(!config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("mongo.{}.", config.stat_prefix());
  Mongo::AccessLogSharedPtr access_log;
  if (!config.access_log().empty()) {
    access_log.reset(new Mongo::AccessLog(config.access_log(), context.accessLogManager()));
  }

  Mongo::FaultConfigSharedPtr fault_config;
  if (config.has_delay()) {
    auto delay = config.delay();
    ASSERT(delay.has_fixed_delay());
    fault_config = std::make_shared<Mongo::FaultConfig>(config.delay());
  }

  return [stat_prefix, &context, access_log,
          fault_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<Mongo::ProdProxyFilter>(
        stat_prefix, context.scope(), context.runtime(), access_log, fault_config,
        context.drainDecision()));
  };
}

NetworkFilterFactoryCb
MongoProxyFilterConfigFactory::createFilterFactory(const Json::Object& json_mongo_proxy,
                                                   FactoryContext& context) {
  envoy::api::v2::filter::network::MongoProxy mongo_proxy;
  Config::FilterJson::translateMongoProxy(json_mongo_proxy, mongo_proxy);

  return createMongoProxyFactory(mongo_proxy, context);
}

NetworkFilterFactoryCb
MongoProxyFilterConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& config,
                                                            FactoryContext& context) {
  return createMongoProxyFactory(
      dynamic_cast<const envoy::api::v2::filter::network::MongoProxy&>(config), context);
}

/**
 * Static registration for the mongo filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<MongoProxyFilterConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
