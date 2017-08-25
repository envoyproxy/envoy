#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"
#include "common/filter/proxy_protocol.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the proxy protocol filter. @see NamedNetworkFilterConfigFactory.
 */
class ProxyProtocolConfigFactory : public NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  ListenerFilterFactoryCb createFilterFactory(const Json::Object&,
                                              FactoryContext& context) override {
    Filter::ProxyProtocol::ConfigSharedPtr config(
        new Filter::ProxyProtocol::Config(context.scope()));
    return [config](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(
          Network::ListenerFilterSharedPtr{new Filter::ProxyProtocol::Instance(config)});
    };
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
