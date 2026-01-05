#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/wildcard/dns_gateway/dns_gateway_filter.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace DnsGateway {

/**
 * Config registration for the DNS Gateway Filter.
 */
class DnsGatewayConfigFactory : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::ListenerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace DnsGateway
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy
