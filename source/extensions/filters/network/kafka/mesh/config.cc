#include "extensions/filters/network/kafka/mesh/config.h"

#include <typeinfo>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#include "extensions/filters/network/kafka/mesh/clustering.h"
#include "extensions/filters/network/kafka/mesh/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

Network::FilterFactoryCb KafkaMeshConfigFactory::createFilterFactoryFromProtoTyped(
    const KafkaMeshProtoConfig& config, Server::Configuration::FactoryContext& context) {

  const ClusteringConfigurationSharedPtr clustering_configuration =
      std::make_shared<ClusteringConfiguration>(config);
  const UpstreamKafkaFacadeSharedPtr upstream_kafka_facade = std::make_shared<UpstreamKafkaFacade>(
      *clustering_configuration, context.threadLocal(), context.api().threadFactory());

  return [clustering_configuration,
          upstream_kafka_facade](Network::FilterManager& filter_manager) -> void {
    Network::ReadFilterSharedPtr filter =
        std::make_shared<KafkaMeshFilter>(*clustering_configuration, *upstream_kafka_facade);
    filter_manager.addReadFilter(filter);
  };
}

/**
 * Static registration for the Kafka filter. @see RegisterFactory.
 */
REGISTER_FACTORY(KafkaMeshConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
