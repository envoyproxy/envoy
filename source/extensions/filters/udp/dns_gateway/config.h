#pragma once

#include "envoy/extensions/filters/udp/dns_gateway/v3/dns_gateway.pb.h"
#include "envoy/extensions/filters/udp/dns_gateway/v3/dns_gateway.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/filters/udp/dns_gateway/dns_gateway_filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsGateway {

/**
 * Config registration for the DNS Gateway Filter.
 */
class DnsGatewayConfigFactory : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::ListenerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace DnsGateway
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
