#include "extensions/quic_listeners/quiche/active_quic_listener_config.h"

#include "envoy/api/v2/listener/quic_config.pb.h"

#include "server/well_known_names.h"

#include "extensions/quic_listeners/quiche/active_quic_listener.h"

namespace Envoy {
namespace Quic {

ProtobufTypes::MessagePtr ActiveQuicListenerConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::api::v2::listener::QuicConfigProto>();
}

Network::ActiveUdpListenerFactoryPtr
ActiveQuicListenerConfigFactory::createActiveUdpListenerFactory(const Protobuf::Message& message) {
  auto& config = dynamic_cast<const envoy::api::v2::listener::QuicConfigProto&>(message);
  return std::make_unique<ActiveQuicListenerFactory>(config);
}

std::string ActiveQuicListenerConfigFactory::name() { return Server::UdpListenerNames::get().Quic; }

REGISTER_FACTORY(ActiveQuicListenerConfigFactory, Server::ActiveUdpListenerConfigFactory);

} // namespace Quic
} // namespace Envoy
