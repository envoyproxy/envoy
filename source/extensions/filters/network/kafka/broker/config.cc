#include "extensions/filters/network/kafka/broker/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#include "extensions/filters/network/kafka/broker/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

Network::FilterFactoryCb KafkaConfigFactory::createFilterFactoryFromProtoTyped(
    const KafkaBrokerProtoConfig& proto_config, Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());

  const std::string& stat_prefix = proto_config.stat_prefix();

  return [&context, stat_prefix](Network::FilterManager& filter_manager) -> void {
    Network::FilterSharedPtr filter =
        std::make_shared<KafkaBrokerFilter>(context.scope(), context.timeSource(), stat_prefix);
    filter_manager.addFilter(filter);
  };
}

/**
 * Static registration for the Kafka filter. @see RegisterFactory.
 */
REGISTER_FACTORY(KafkaConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
