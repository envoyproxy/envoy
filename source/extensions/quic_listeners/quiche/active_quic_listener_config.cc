#include "extensions/quic_listeners/quiche/active_quic_listener_config.h"

#include "envoy/config/listener/v3/quic_config.pb.h"

#include "extensions/quic_listeners/quiche/active_quic_listener.h"

namespace Envoy {
namespace Quic {

ProtobufTypes::MessagePtr ActiveQuicListenerConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::listener::v3::QuicProtocolOptions>();
}

Network::ActiveUdpListenerFactoryPtr
ActiveQuicListenerConfigFactory::createActiveUdpListenerFactory(const Protobuf::Message& message) {
  auto& config = dynamic_cast<const envoy::config::listener::v3::QuicProtocolOptions&>(message);
  return std::make_unique<ActiveQuicListenerFactory>(config);
}

std::string ActiveQuicListenerConfigFactory::name() const { return QuicListenerName; }

REGISTER_FACTORY(ActiveQuicListenerConfigFactory, Server::ActiveUdpListenerConfigFactory);

} // namespace Quic
} // namespace Envoy
