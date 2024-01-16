#include "contrib/kafka/filters/network/source/broker/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#include "contrib/kafka/filters/network/source/broker/filter.h"
#include "contrib/kafka/filters/network/source/broker/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

Network::FilterFactoryCb KafkaConfigFactory::createFilterFactoryFromProtoTyped(
    const KafkaBrokerProtoConfig& proto_config, Server::Configuration::FactoryContext& context) {

  const BrokerFilterConfigSharedPtr filter_config =
      std::make_shared<BrokerFilterConfig>(proto_config);
  return [&context, filter_config](Network::FilterManager& filter_manager) -> void {
    Network::FilterSharedPtr filter = std::make_shared<KafkaBrokerFilter>(
        context.scope(), context.serverFactoryContext().timeSource(), *filter_config);
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
