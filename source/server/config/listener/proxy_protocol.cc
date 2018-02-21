#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"
#include "common/filter/listener/proxy_protocol.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the proxy protocol filter. @see NamedNetworkFilterConfigFactory.
 */
class ProxyProtocolConfigFactory : public NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  ListenerFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message&,
                                                       FactoryContext& context) override {
    Filter::Listener::ProxyProtocol::ConfigSharedPtr config(
        new Filter::Listener::ProxyProtocol::Config(context.scope()));
    return [config](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(
          std::make_unique<Filter::Listener::ProxyProtocol::Instance>(config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Empty>();
  }

  std::string name() override { return Config::ListenerFilterNames::get().PROXY_PROTOCOL; }
};

/**
 * Static registration for the proxy protocol filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ProxyProtocolConfigFactory, NamedListenerFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
