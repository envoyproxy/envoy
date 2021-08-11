#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

/**
 * Config registration for the UDP proxy filter. @see NamedUdpListenerFilterConfigFactory.
 */
class UdpProxyFilterConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::ListenerFactoryContext& context) override {
    auto shared_config = std::make_shared<UdpProxyFilterConfig>(
        context.clusterManager(), context.timeSource(), context.scope(),
        MessageUtil::downcastAndValidate<
            const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig&>(
            config, context.messageValidationVisitor()));
    return [shared_config](Network::UdpListenerFilterManager& filter_manager,
                           Network::UdpReadFilterCallbacks& callbacks) -> void {
      filter_manager.addReadFilter(std::make_unique<UdpProxyFilter>(callbacks, shared_config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig>();
  }

  std::string name() const override { return "envoy.filters.udp_listener.udp_proxy"; }
};

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
