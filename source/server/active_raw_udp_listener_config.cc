#include "server/active_raw_udp_listener_config.h"

#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Server {

Network::ConnectionHandler::ActiveListenerPtr
ActiveRawUdpListenerFactory::createActiveUdpListener(Network::ConnectionHandler& parent,
                                                     Network::ListenerConfig& config) const {
  return std::make_unique<ConnectionHandlerImpl::ActiveUdpListener>(
      dynamic_cast<ConnectionHandlerImpl&>(parent), config);
}

Network::ActiveUdpListenerFactoryPtr
ActiveRawUdpListenerConfigFactory::createActiveUdpListenerFactory(
    const Protobuf::Message& /*message*/) {
  return std::make_unique<Server::ActiveRawUdpListenerFactory>();
}

std::string ActiveRawUdpListenerConfigFactory::name() { return "raw_udp_listener"; }

REGISTER_FACTORY(ActiveRawUdpListenerConfigFactory, Server::ActiveUdpListenerConfigFactory);

} // namespace Server
} // namespace Envoy
