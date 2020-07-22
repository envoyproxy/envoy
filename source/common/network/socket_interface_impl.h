#pragma once

#include "envoy/network/address.h"
#include "envoy/network/socket.h"

#include "common/network/socket_interface.h"

namespace Envoy {
namespace Network {

class SocketInterfaceImpl : public SocketInterfaceBase {
public:
  // SocketInterface
  IoHandlePtr socket(Socket::Type socket_type, Address::Type addr_type, Address::IpVersion version,
                     bool socket_v6only) override;
  IoHandlePtr socket(Socket::Type socket_type, const Address::InstanceConstSharedPtr addr) override;
  IoHandlePtr socket(os_fd_t fd) override;
  bool ipFamilySupported(int domain) override;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.extensions.network.socket_interface.default_socket_interface";
  };
};

DECLARE_FACTORY(SocketInterfaceImpl);

} // namespace Network
} // namespace Envoy