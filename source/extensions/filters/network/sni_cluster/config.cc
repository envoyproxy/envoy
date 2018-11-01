#include "extensions/filters/network/sni_cluster/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/sni_cluster/sni_cluster.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

Network::FilterFactoryCb SniClusterNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::sni_cluster::v2::SniCluster& proto_config,
    Server::Configuration::FactoryContext&) {
  SniClusterFilterConfigSharedPtr config(std::make_shared<SniClusterFilterConfig>(proto_config));
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<SniClusterFilter>(config));
  };
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
