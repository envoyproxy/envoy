#pragma once

#include "envoy/common/io/io_uring.h"
#include "envoy/network/socket.h"

#include "source/common/network/socket_interface.h"

namespace Envoy {
namespace Network {

class SocketInterfaceImpl : public SocketInterfaceBase {
public:
  // SocketInterface
  IoHandlePtr socket(Socket::Type socket_type, Address::Type addr_type, Address::IpVersion version,
                     bool socket_v6only, const SocketCreationOptions& options) const override;
  IoHandlePtr socket(Socket::Type socket_type, const Address::InstanceConstSharedPtr addr,
                     const SocketCreationOptions& options) const override;
  bool ipFamilySupported(int domain) override;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.extensions.network.socket_interface.default_socket_interface";
  };

  static IoHandlePtr
  makePlatformSpecificSocket(int socket_fd, bool socket_v6only, absl::optional<int> domain,
                             const SocketCreationOptions& options,
                             Io::IoUringWorkerFactory* io_uring_worker_factory = nullptr);

protected:
  virtual IoHandlePtr makeSocket(int socket_fd, bool socket_v6only, absl::optional<int> domain,
                                 const SocketCreationOptions& options) const;
};

DECLARE_FACTORY(SocketInterfaceImpl);

} // namespace Network
} // namespace Envoy
