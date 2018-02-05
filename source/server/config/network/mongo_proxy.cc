#include "server/config/network/mongo_proxy.h"

#include <string>

#include "envoy/config/filter/network/mongo_proxy/v2/mongo_proxy.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/common/fmt.h"
#include "common/config/filter_json.h"
#include "common/mongo/proxy.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb MongoProxyFilterConfigFactory::createFilter(
    const envoy::config::filter::network::mongo_proxy::v2::MongoProxy& proto_config,
    FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("mongo.{}.", proto_config.stat_prefix());
  Mongo::AccessLogSharedPtr access_log;
  if (!proto_config.access_log().empty()) {
    access_log.reset(new Mongo::AccessLog(proto_config.access_log(), context.accessLogManager()));
  }

  Mongo::FaultConfigSharedPtr fault_config;
  if (proto_config.has_delay()) {
    auto delay = proto_config.delay();
    ASSERT(delay.has_fixed_delay());
    fault_config = std::make_shared<Mongo::FaultConfig>(proto_config.delay());
  }

  return [stat_prefix, &context, access_log,
          fault_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<Mongo::ProdProxyFilter>(
        stat_prefix, context.scope(), context.runtime(), access_log, fault_config,
        context.drainDecision()));
  };
}

NetworkFilterFactoryCb
MongoProxyFilterConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                   FactoryContext& context) {
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy proto_config;
  Config::FilterJson::translateMongoProxy(json_config, proto_config);
  return createFilter(proto_config, context);
}

NetworkFilterFactoryCb
MongoProxyFilterConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                            FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::network::mongo_proxy::v2::MongoProxy&>(proto_config),
      context);
}

/**
 * Static registration for the mongo filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<MongoProxyFilterConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
