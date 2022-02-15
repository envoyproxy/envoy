#include "contrib/vcl/source/config.h"

#include "contrib/envoy/extensions/vcl/v3alpha/vcl_socket_interface.pb.h"
#include "contrib/vcl/source/vcl_interface.h"
#include "contrib/vcl/source/vcl_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

Server::BootstrapExtensionPtr
VclSocketInterface::createBootstrapExtension(const Protobuf::Message&,
                                             Server::Configuration::ServerFactoryContext& ctx) {

  vclInterfaceInit(ctx.mainThreadDispatcher(), ctx.options().concurrency());
  return std::make_unique<VclSocketInterfaceExtension>(*this);
}

ProtobufTypes::MessagePtr VclSocketInterface::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::vcl::v3alpha::VclSocketInterface>();
}

Envoy::Network::IoHandlePtr VclSocketInterface::socket(
    Envoy::Network::Socket::Type socket_type, Envoy::Network::Address::Type addr_type,
    Envoy::Network::Address::IpVersion, bool, const Envoy::Network::SocketCreationOptions&) const {
  if (vppcom_worker_index() == -1) {
    vclInterfaceWorkerRegister();
  }
  VCL_LOG("trying to create socket1 epoll fd {}", vppcom_mq_epoll_fd());
  if (addr_type == Envoy::Network::Address::Type::Pipe) {
    return nullptr;
  }
  uint32_t sh = vppcom_session_create(
      socket_type == Envoy::Network::Socket::Type::Stream ? VPPCOM_PROTO_TCP : VPPCOM_PROTO_UDP, 1);
  if (!VCL_SH_VALID(sh)) {
    return nullptr;
  }
  return std::make_unique<VclIoHandle>(sh, VclInvalidFd);
}

Envoy::Network::IoHandlePtr
VclSocketInterface::socket(Envoy::Network::Socket::Type socket_type,
                           const Envoy::Network::Address::InstanceConstSharedPtr addr,
                           const Envoy::Network::SocketCreationOptions& creation_options) const {
  return socket(socket_type, addr->type(), Envoy::Network::Address::IpVersion::v4, false,
                creation_options);
}

bool VclSocketInterface::ipFamilySupported(int) { return true; };

REGISTER_FACTORY(VclSocketInterface, Server::Configuration::BootstrapExtensionFactory);

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
