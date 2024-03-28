#include "source/extensions/filters/network/sni_cluster/config.h"

#include "envoy/extensions/filters/network/sni_cluster/v3/sni_cluster.pb.h"
#include "envoy/extensions/filters/network/sni_cluster/v3/sni_cluster.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/sni_cluster/sni_cluster.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

absl::StatusOr<Network::FilterFactoryCb>
SniClusterNetworkFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message&, Server::Configuration::FactoryContext&) {
  return [](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<SniClusterFilter>());
  };
}

ProtobufTypes::MessagePtr SniClusterNetworkFilterConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::filters::network::sni_cluster::v3::SniCluster>();
}

/**
 * Static registration for the sni_cluster filter. @see RegisterFactory.
 */
REGISTER_FACTORY(SniClusterNetworkFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
