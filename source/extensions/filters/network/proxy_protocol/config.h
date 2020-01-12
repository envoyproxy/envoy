#pragma once

#include "envoy/server/filter_config.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ProxyProtocol {

/**
 * Config registration for the proxy protocol upstream network filter. @see
 * NamedUpstreamNetworkFilterConfigFactory.
 */
class ProxyProtocolConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::CommonFactoryContext&) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace ProxyProtocol
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
