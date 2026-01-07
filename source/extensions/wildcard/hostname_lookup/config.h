#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/wildcard/hostname_lookup/hostname_lookup.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace HostnameLookup {

/**
 * Config registration for the Hostname Lookup Filter.
 */
class HostnameLookupConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace HostnameLookup
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy
