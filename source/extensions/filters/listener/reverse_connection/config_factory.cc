#include "source/extensions/filters/listener/reverse_connection/config_factory.h"

#include "envoy/extensions/filters/listener/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/extensions/filters/listener/reverse_connection/v3/reverse_connection.pb.validate.h"

#include "source/extensions/filters/listener/reverse_connection/config.h"
#include "source/extensions/filters/listener/reverse_connection/reverse_connection.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ReverseConnection {

Network::ListenerFilterFactoryCb
ReverseConnectionConfigFactory::createListenerFilterFactoryFromProto(
    const Protobuf::Message& message,
    const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
    Server::Configuration::ListenerFactoryContext& context) {
  auto proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::listener::reverse_connection::v3::ReverseConnection&>(
      message, context.messageValidationVisitor());

  // TODO(Basu): Remove dependency on ReverseConnRegistry singleton
  // Retrieve the ReverseConnRegistry singleton and acecss the thread local slot
  // std::shared_ptr<ReverseConnection::ReverseConnRegistry> reverse_conn_registry =
  //     context.serverFactoryContext()
  //         .singletonManager()
  //         .getTyped<ReverseConnection::ReverseConnRegistry>("reverse_conn_registry_singleton");
  // if (reverse_conn_registry == nullptr) {
  //   throw EnvoyException(
  //       "Cannot create reverse connection listener filter. Reverse connection registry not
  //       found");
  // }

  Config config(proto_config);
  return [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
    filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config));
  };
}

ProtobufTypes::MessagePtr ReverseConnectionConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::filters::listener::reverse_connection::v3::ReverseConnection>();
}

REGISTER_FACTORY(ReverseConnectionConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory);

} // namespace ReverseConnection
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
