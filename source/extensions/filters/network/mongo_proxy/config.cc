#include "source/extensions/filters/network/mongo_proxy/config.h"

#include <memory>

#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.h"
#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/extensions/filters/network/mongo_proxy/proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

Network::FilterFactoryCb MongoProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("mongo.{}", proto_config.stat_prefix());
  AccessLogSharedPtr access_log;
  if (!proto_config.access_log().empty()) {
    access_log = std::make_shared<AccessLog>(
        proto_config.access_log(), context.serverFactoryContext().accessLogManager(),
        context.serverFactoryContext().mainThreadDispatcher().timeSource());
  }

  Filters::Common::Fault::FaultDelayConfigSharedPtr fault_config;
  if (proto_config.has_delay()) {
    fault_config = std::make_shared<Filters::Common::Fault::FaultDelayConfig>(proto_config.delay());
  }

  auto commands = std::vector<std::string>{"delete", "insert", "update"};
  if (proto_config.commands_size() > 0) {
    commands =
        std::vector<std::string>(proto_config.commands().begin(), proto_config.commands().end());
  }

  auto stats = std::make_shared<MongoStats>(context.scope(), stat_prefix, commands);
  const bool emit_dynamic_metadata = proto_config.emit_dynamic_metadata();
  return [stat_prefix, &context, access_log, fault_config, emit_dynamic_metadata,
          stats](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<ProdProxyFilter>(
        stat_prefix, context.scope(), context.serverFactoryContext().runtime(), access_log,
        fault_config, context.drainDecision(),
        context.serverFactoryContext().mainThreadDispatcher().timeSource(), emit_dynamic_metadata,
        stats));
  };
}

/**
 * Static registration for the mongo filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(MongoProxyFilterConfigFactory,
                        Server::Configuration::NamedNetworkFilterConfigFactory,
                        "envoy.mongo_proxy");

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
