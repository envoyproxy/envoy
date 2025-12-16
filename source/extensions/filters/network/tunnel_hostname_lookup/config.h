#pragma once

#include "envoy/extensions/filters/network/tunnel_hostname_lookup/v3/tunnel_hostname_lookup.pb.h"
#include "envoy/extensions/filters/network/tunnel_hostname_lookup/v3/tunnel_hostname_lookup.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/tunnel_hostname_lookup/tunnel_hostname_lookup.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TunnelHostnameLookup {

/**
 * Config registration for the Tunnel Hostname Lookup filter.
 */
class TunnelHostnameLookupConfigFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace TunnelHostnameLookup
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
