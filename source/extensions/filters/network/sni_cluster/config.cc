#include "extensions/filters/network/sni_cluster/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/sni_cluster/sni_cluster.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

Network::FilterFactoryCb
SniClusterNetworkFilterConfigFactory::createFilterFactory(const Json::Object&,
                                                          Server::Configuration::FactoryContext&) {
  // Only used in v1 filters.
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Network::FilterFactoryCb SniClusterNetworkFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message&, Server::Configuration::FactoryContext&) {
  return [](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<SniClusterFilter>());
  };
}

ProtobufTypes::MessagePtr SniClusterNetworkFilterConfigFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::Empty>();
}

/**
 * Static registration for the sni_cluster filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<SniClusterNetworkFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
