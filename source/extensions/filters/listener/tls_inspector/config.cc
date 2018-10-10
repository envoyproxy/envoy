#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/listener/tls_inspector/tls_inspector.h"
#include "extensions/filters/listener/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

/**
 * Config registration for the TLS inspector filter. @see NamedNetworkFilterConfigFactory.
 */
class TlsInspectorConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
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
    return std::make_unique<Envoy::ProtobufWkt::Empty>();
  }

  std::string name() override { return ListenerFilterNames::get().TlsInspector; }
};

/**
 * Static registration for the TLS inspector filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<TlsInspectorConfigFactory,
                                 Server::Configuration::NamedListenerFilterConfigFactory>
    registered_;

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
