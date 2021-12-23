#include "envoy/extensions/filters/listener/connect_handler/v3/connect_handler.pb.h"
#include "envoy/extensions/filters/listener/connect_handler/v3/connect_handler.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/listener/connect_handler/connect_handler.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ConnectHandler {

/**
 * Config registration for the Connect Handler filter. @see NamedNetworkFilterConfigFactory.
 */
class ConnectHandlerConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message&,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {
    ConfigSharedPtr config(std::make_shared<Config>(context.scope()));
    return
        [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
          filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config));
        };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::listener::connect_handler::v3::ConnectHandler>();
  }

  std::string name() const override { return "envoy.filters.listener.connect_handler"; }
};

/**
 * Static registration for the connect handler filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ConnectHandlerConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.connect_handler"};

} // namespace ConnectHandler
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
