#include <memory>

#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.validate.h"
#include "envoy/extensions/filters/udp/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/extensions/filters/udp/proxy_protocol/v3/proxy_protocol.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/listener/proxy_protocol/proxy_protocol.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

/**
 * Config registration for the proxy protocol filter. @see NamedNetworkFilterConfigFactory.
 */
class ProxyProtocolConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& message,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {

    // downcast it to the proxy protocol config
    const auto& proto_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol&>(
        message, context.messageValidationVisitor());

    ConfigSharedPtr config = std::make_shared<Config>(context.scope(), proto_config);
    return
        [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
          filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config));
        };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol>();
  }

  std::string name() const override { return "envoy.filters.listener.proxy_protocol"; }
};

class UdpProxyProtocolConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::udp::proxy_protocol::v3::ProxyProtocol>();
  }

  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& message,
                               Server::Configuration::ListenerFactoryContext& context) override {
    // downcast it to the proxy protocol config
    const auto& proto_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::udp::proxy_protocol::v3::ProxyProtocol&>(
        message, context.messageValidationVisitor());
    UdpConfigSharedPtr config = std::make_shared<UdpConfig>(proto_config);
    return [config](Network::UdpListenerFilterManager& manager,
                    Network::UdpReadFilterCallbacks& callbacks) -> void {
      manager.addReadFilter(std::make_unique<UdpFilter>(callbacks, config));
    };
  }

  std::string name() const override { return "envoy.filters.udp_listener.proxy_protocol"; }
};

/**
 * Static registration for the proxy protocol filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ProxyProtocolConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.proxy_protocol"};

static Registry::RegisterFactory<UdpProxyProtocolConfigFactory,
                                 Server::Configuration::NamedUdpListenerFilterConfigFactory>
    register_udp_;

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
