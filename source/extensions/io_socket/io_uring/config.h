#pragma once

#include "envoy/extensions/network/socket_interface/v3/io_uring_socket_interface.pb.h"

#include "source/common/network/socket_interface.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace IoUring {

class IoUringFactory;

class SocketInterfaceImpl : public Network::SocketInterfaceBase {
public:
  // SocketInterface
  Network::IoHandlePtr socket(Network::Socket::Type socket_type, Network::Address::Type addr_type,
                              Network::Address::IpVersion version, bool socket_v6only,
                              const Network::SocketCreationOptions& options) const override;
  Network::IoHandlePtr socket(Network::Socket::Type socket_type,
                              const Network::Address::InstanceConstSharedPtr addr,
                              const Network::SocketCreationOptions& options) const override;
  bool ipFamilySupported(int domain) override;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& message,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.extensions.network.socket_interface.io_uring";
  };

private:
  uint32_t read_buffer_size_;
  std::unique_ptr<const IoUringFactory> io_uring_factory_;
};

DECLARE_FACTORY(SocketInterfaceImpl);

} // namespace IoUring
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
