#pragma once

#include "envoy/server/bootstrap_extension_config.h"

namespace Envoy {
namespace Network {

class SocketInterfaceFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.bootstrap.socket_interface"; }
};

} // namespace Network
} // namespace Envoy