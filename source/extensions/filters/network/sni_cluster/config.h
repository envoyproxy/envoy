#pragma once

#include "envoy/config/filter/network/sni_cluster/v2/sni_cluster.pb.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

/**
 * Config registration for the sni_cluster filter. @see NamedNetworkFilterConfigFactory.
 */
class SniClusterNetworkFilterConfigFactory
    : public Common::FactoryBase<envoy::config::filter::network::sni_cluster::v2::SniCluster> {

public:
  SniClusterNetworkFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().SniCluster) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::sni_cluster::v2::SniCluster& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
