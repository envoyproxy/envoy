#include "contrib/reverse_connection//filters/listener/source/config_factory.h"

#include "contrib/envoy/extensions/filters/listener/reverse_connection/v3alpha/reverse_connection.pb.h"
#include "contrib/envoy/extensions/filters/listener/reverse_connection/v3alpha/reverse_connection.pb.validate.h"

#include "contrib/reverse_connection//filters/listener/source/config.h"
#include "contrib/reverse_connection//filters/listener/source/reverse_connection.h"

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
      const envoy::extensions::filters::listener::reverse_connection::v3alpha::ReverseConnection&>(
      message, context.messageValidationVisitor());
  // Retrieve the ReverseConnRegistry singleton and acecss the thread local slot
  std::shared_ptr<ReverseConnection::ReverseConnRegistry> reverse_conn_registry =
        context.serverFactoryContext().singletonManager().getTyped<ReverseConnection::ReverseConnRegistry>("reverse_conn_registry_singleton");
  if (reverse_conn_registry == nullptr) {
    throw EnvoyException(
      "Cannot create reverse connection listener filter. Reverse connection registry not found");
  }
  // ReverseConnection::RCThreadLocalRegistry* thread_local_registry = reverse_conn_registry->getLocalRegistry();
  // if (thread_local_registry == nullptr) {
  //   throw EnvoyException("Cannot create reverse connection listener filter. Thread local reverse connection registry is null");
  // }
  Config config(proto_config);
  return [listener_filter_matcher, config, reverse_conn_registry](Network::ListenerFilterManager& filter_manager) -> void {
    filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config, reverse_conn_registry));
  };
}

ProtobufTypes::MessagePtr ReverseConnectionConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::filters::listener::reverse_connection::v3alpha::ReverseConnection>();
}

REGISTER_FACTORY(ReverseConnectionConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory);

} // namespace ReverseConnection
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
