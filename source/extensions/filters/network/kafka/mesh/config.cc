#include "extensions/filters/network/kafka/mesh/config.h"

#include <typeinfo>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#ifndef WIN32
#include "extensions/filters/network/kafka/mesh/clustering.h"
#include "extensions/filters/network/kafka/mesh/filter.h"
#endif

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// The mesh filter doesn't do anything special, it just sets up the shared entities.
// Any extra configuration validation is done in ClusteringConfiguration constructor.
Network::FilterFactoryCb KafkaMeshConfigFactory::createFilterFactoryFromProtoTyped(
    const KafkaMeshProtoConfig& config, Server::Configuration::FactoryContext& context) {

#ifdef WIN32
  throw "boom";
#else
  ENVOY_LOG(warn, "Creating ClusteringConfiguration instance");
  const ClusteringConfigurationSharedPtr clustering_configuration =
      std::make_shared<ClusteringConfigurationImpl>(config);
  ENVOY_LOG(warn, "Creating UpstreamKafkaFacadeImpl instance");
  const UpstreamKafkaFacadeSharedPtr upstream_kafka_facade =
      std::make_shared<UpstreamKafkaFacadeImpl>(*clustering_configuration, context.threadLocal(),
                                                context.api().threadFactory());
  ENVOY_LOG(warn, "Shared instances have been created");

  return [clustering_configuration,
          upstream_kafka_facade](Network::FilterManager& filter_manager) -> void {
    Network::ReadFilterSharedPtr filter =
        std::make_shared<KafkaMeshFilter>(*clustering_configuration, *upstream_kafka_facade);
    filter_manager.addReadFilter(filter);
  };
#endif
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
