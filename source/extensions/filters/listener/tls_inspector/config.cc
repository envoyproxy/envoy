#include <string>

#include "envoy/extensions/filters/listener/tls_inspector/v3/tls_inspector.pb.h"
#include "envoy/extensions/filters/listener/tls_inspector/v3/tls_inspector.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/listener/tls_inspector/tls_inspector.h"

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
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& message,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {

    // downcast it to the TLS inspector config
    const auto& proto_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector&>(
        message, context.messageValidationVisitor());

    ConfigSharedPtr config = std::make_shared<Config>(context.scope(), proto_config);
    return
        [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
          filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config));
        };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector>();
  }

  std::string name() const override { return "envoy.filters.listener.tls_inspector"; }
};

/**
 * Static registration for the TLS inspector filter. @see RegisterFactory.
 */
REGISTER_FACTORY(TlsInspectorConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.tls_inspector"};

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
