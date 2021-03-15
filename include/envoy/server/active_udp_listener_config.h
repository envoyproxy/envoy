#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/connection_handler.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {

/**
 * Interface to create UDP listener.
 */
class ActiveUdpListenerConfigFactory : public Config::TypedFactory {
public:
  /**
   * Create an ActiveUdpListenerFactory object according to given message.
   * @param message specifies QUIC protocol options in a protobuf.
   * @param concurrency is the number of listeners instances to be created.
   */
  virtual Network::ActiveUdpListenerFactoryPtr
  createActiveUdpListenerFactory(const Protobuf::Message& message, uint32_t concurrency) PURE;

  std::string category() const override { return "envoy.udp_listeners"; }
};

} // namespace Server
} // namespace Envoy
