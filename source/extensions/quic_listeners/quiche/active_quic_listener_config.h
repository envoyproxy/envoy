#pragma once

#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/active_udp_listener_config.h"

namespace Envoy {
namespace Quic {

const std::string QuicListenerName{"quiche_quic_listener"};

// A factory to create ActiveQuicListenerFactory based on given protobuf.
class ActiveQuicListenerConfigFactory : public Server::ActiveUdpListenerConfigFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  Network::ActiveUdpListenerFactoryPtr
  createActiveUdpListenerFactory(const Protobuf::Message&, uint32_t concurrency) override;

  std::string name() const override;
};

DECLARE_FACTORY(ActiveQuicListenerConfigFactory);

} // namespace Quic
} // namespace Envoy
