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

  std::string category() const override {
    static const char FACTORY_CATEGORY[] = "udp_listeners";
    return FACTORY_CATEGORY;
  }
};

} // namespace Server
} // namespace Envoy
