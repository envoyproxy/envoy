#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/listener/proxy_protocol/proxy_protocol.h"
#include "extensions/filters/listener/well_known_names.h"

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
  Network::ListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::ListenerFactoryContext& context) override {
    ConfigSharedPtr config(new Config(context.scope()));
    return [config](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(std::make_unique<Filter>(config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol>();
  }

  std::string name() const override { return ListenerFilterNames::get().ProxyProtocol; }
};

/**
 * Static registration for the proxy protocol filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ProxyProtocolConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.proxy_protocol"};

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
