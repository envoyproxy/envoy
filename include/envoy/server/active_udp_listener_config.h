#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/connection_handler.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {

/**
 * Interface to create udp listener according to
 * envoy::api::v2::listener::UdpListenerConfig.udp_listener_name.
 */
class ActiveUdpListenerConfigFactory : public Config::UntypedFactory {
public:
  virtual ~ActiveUdpListenerConfigFactory() = default;

  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * Create an ActiveUdpListenerFactory object according to given message.
   */
  virtual Network::ActiveUdpListenerFactoryPtr
  createActiveUdpListenerFactory(const Protobuf::Message& message) PURE;

  std::string category() const override { return "udp_listeners"; }
};

} // namespace Server
} // namespace Envoy
