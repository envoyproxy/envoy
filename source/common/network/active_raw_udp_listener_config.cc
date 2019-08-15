#include "common/network/active_raw_udp_listener_config.h"

namespace Envoy {
namespace Network {

Server::ActiveUdpListenerFactoryPtr
ActiveRawUdpListenerConfigFactory::createActiveUdpListenerFactory(
    const Protobuf::Message& /*message*/) {
  return std::make_unique<Server::ActiveRawUdpListenerFactory>();
}

std::string ActiveRawUdpListenerConfigFactory::name() { return "raw_udp_listener"; }

REGISTER_FACTORY(ActiveRawUdpListenerConfigFactory, Server::ActiveUdpListenerConfigFactory);

} // namespace Network
} // namespace Envoy
