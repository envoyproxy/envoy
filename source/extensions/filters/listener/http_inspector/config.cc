#include "envoy/extensions/filters/listener/http_inspector/v3/http_inspector.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/listener/http_inspector/http_inspector.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

/**
 * Config registration for the Http inspector filter. @see NamedNetworkFilterConfigFactory.
 */
class HttpInspectorConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& message,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {
    // downcast it to the HTTP inspector config
    const auto& proto_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::listener::http_inspector::v3::HttpInspector&>(
        message, context.messageValidationVisitor());

    ConfigSharedPtr config(std::make_shared<Config>(context.scope(), proto_config));
    return
        [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
          filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config));
        };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::listener::http_inspector::v3::HttpInspector>();
  }

  std::string name() const override { return "envoy.filters.listener.http_inspector"; }
};

/**
 * Static registration for the http inspector filter. @see RegisterFactory.
 */
REGISTER_FACTORY(HttpInspectorConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.http_inspector"};

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
