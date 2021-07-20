#include "source/extensions/filters/network/kafka/mesh/config.h"

#include <typeinfo>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#ifndef WIN32
#include "source/extensions/filters/network/kafka/mesh/upstream_config.h"
#include "source/extensions/filters/network/kafka/mesh/upstream_kafka_facade.h"
#include "source/extensions/filters/network/kafka/mesh/filter.h"
#else
#include "envoy/common/exception.h"
#endif

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// The mesh filter doesn't do anything special, it just sets up the shared entities.
// Any extra configuration validation is done in UpstreamKafkaConfiguration constructor.
Network::FilterFactoryCb KafkaMeshConfigFactory::createFilterFactoryFromProtoTyped(
    const KafkaMeshProtoConfig& config, Server::Configuration::FactoryContext& context) {

#ifdef WIN32
  throw EnvoyException("Kafka mesh filter is not ready for Windows");
#else
  const UpstreamKafkaConfigurationSharedPtr configuration =
      std::make_shared<UpstreamKafkaConfigurationImpl>(config);
  const UpstreamKafkaFacadeSharedPtr upstream_kafka_facade =
      std::make_shared<UpstreamKafkaFacadeImpl>(*configuration, context.threadLocal(),
                                                context.api().threadFactory());

  return [configuration, upstream_kafka_facade](Network::FilterManager& filter_manager) -> void {
    Network::ReadFilterSharedPtr filter =
        std::make_shared<KafkaMeshFilter>(*configuration, *upstream_kafka_facade);
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
