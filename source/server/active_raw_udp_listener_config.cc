#include "server/active_raw_udp_listener_config.h"

#include <memory>
#include <string>

#include "envoy/api/v2/listener/listener.pb.h"

#include "server/connection_handler_impl.h"
#include "server/well_known_names.h"

namespace Envoy {
namespace Server {

Network::ConnectionHandler::ActiveListenerPtr
ActiveRawUdpListenerFactory::createActiveUdpListener(Network::ConnectionHandler& parent,
                                                     Event::Dispatcher& dispatcher,
                                                     Network::ListenerConfig& config) {
  return std::make_unique<ActiveUdpListener>(parent, dispatcher, config);
}

ProtobufTypes::MessagePtr ActiveRawUdpListenerConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::listener::v3::ActiveRawUdpListenerConfig>();
}

Network::ActiveUdpListenerFactoryPtr
ActiveRawUdpListenerConfigFactory::createActiveUdpListenerFactory(
    const Protobuf::Message& /*message*/, uint32_t /*concurrency*/) {
  return std::make_unique<Server::ActiveRawUdpListenerFactory>();
}

std::string ActiveRawUdpListenerConfigFactory::name() const {
  return UdpListenerNames::get().RawUdp;
}

REGISTER_FACTORY(ActiveRawUdpListenerConfigFactory, Server::ActiveUdpListenerConfigFactory);

} // namespace Server
} // namespace Envoy
