#pragma once

#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

/**
 * Config registration for the sni_cluster filter. @see NamedNetworkFilterConfigFactory.
 */
class SniClusterNetworkFilterConfigFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return NetworkFilterNames::get().SniCluster; }
};

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
