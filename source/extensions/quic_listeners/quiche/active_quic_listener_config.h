#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/active_udp_listener_config.h"

namespace Envoy {
namespace Quic {

// A factory to create ActiveQuicListenerFactory based on given protobuf.
class ActiveQuicListenerConfigFactory : public Server::ActiveUdpListenerConfigFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  Network::ActiveUdpListenerFactoryPtr
  createActiveUdpListenerFactory(const Protobuf::Message&) override;

  std::string name() override;
};

DECLARE_FACTORY(ActiveQuicListenerConfigFactory);

} // namespace Quic
} // namespace Envoy
