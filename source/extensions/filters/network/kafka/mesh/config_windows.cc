#include <typeinfo>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#include "extensions/filters/network/kafka/mesh/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

Network::FilterFactoryCb
KafkaMeshConfigFactory::createFilterFactoryFromProtoTyped(const KafkaMeshProtoConfig&,
                                                          Server::Configuration::FactoryContext&) {

  throw "boom";
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
