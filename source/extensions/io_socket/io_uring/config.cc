#include "source/extensions/io_socket/io_uring/config.h"

#include "envoy/common/platform.h"
#include "envoy/extensions/network/socket_interface/v3/io_uring_socket_interface.pb.validate.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/extensions/io_socket/io_uring/io_handle_impl.h"

#if defined(__linux__)
#include "source/common/io/io_uring_impl.h"
#endif

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace IoUring {

namespace {

constexpr uint32_t DefaultReadBufferSize = 8192;

} // namespace

Network::IoHandlePtr
SocketInterfaceImpl::socket(Network::Socket::Type socket_type, Network::Address::Type addr_type,
                            Network::Address::IpVersion version, bool socket_v6only,
                            const Network::SocketCreationOptions& options) const {
  int protocol = 0;
  int flags = 0;

  if (options.mptcp_enabled_) {
    ASSERT(socket_type == Network::Socket::Type::Stream);
    ASSERT(addr_type == Network::Address::Type::Ip);
    protocol = IPPROTO_MPTCP;
  }

  if (socket_type == Network::Socket::Type::Stream) {
    flags |= SOCK_STREAM;
  } else {
    flags |= SOCK_DGRAM;
  }

  int domain;
  if (addr_type == Network::Address::Type::Ip) {
    if (version == Network::Address::IpVersion::v6) {
      domain = AF_INET6;
    } else {
      ASSERT(version == Network::Address::IpVersion::v4);
      domain = AF_INET;
    }
  } else if (addr_type == Network::Address::Type::Pipe) {
    domain = AF_UNIX;
  } else {
    PANIC("not implemented");
  }

  const Api::SysCallSocketResult result =
      Api::OsSysCallsSingleton::get().socket(domain, flags, protocol);
  RELEASE_ASSERT(SOCKET_VALID(result.return_value_),
                 fmt::format("socket(2) failed, got error: {}", errorDetails(result.errno_)));

  ASSERT(io_uring_factory_ != nullptr);
  return std::make_unique<IoUringSocketHandleImpl>(read_buffer_size_, *io_uring_factory_,
                                                   result.return_value_, socket_v6only, domain);
}

Network::IoHandlePtr
SocketInterfaceImpl::socket(Network::Socket::Type socket_type,
                            const Network::Address::InstanceConstSharedPtr addr,
                            const Network::SocketCreationOptions& options) const {
  Network::Address::IpVersion ip_version =
      addr->ip() ? addr->ip()->version() : Network::Address::IpVersion::v4;
  int v6only = 0;
  if (addr->type() == Network::Address::Type::Ip && ip_version == Network::Address::IpVersion::v6) {
    v6only = addr->ip()->ipv6()->v6only();
  }

  Network::IoHandlePtr io_handle =
      SocketInterfaceImpl::socket(socket_type, addr->type(), ip_version, v6only, options);
  if (addr->type() == Network::Address::Type::Ip && ip_version == Network::Address::IpVersion::v6) {
    // Setting IPV6_V6ONLY restricts the IPv6 socket to IPv6 connections only.
    const Api::SysCallIntResult result = io_handle->setOption(
        IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof(v6only));
    RELEASE_ASSERT(!SOCKET_FAILURE(result.return_value_), "");
  }
  return io_handle;
}

bool SocketInterfaceImpl::ipFamilySupported(int domain) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSocketResult result = os_sys_calls.socket(domain, SOCK_STREAM, 0);
  if (SOCKET_VALID(result.return_value_)) {
    RELEASE_ASSERT(
        os_sys_calls.close(result.return_value_).return_value_ == 0,
        fmt::format("Fail to close fd: response code {}", errorDetails(result.return_value_)));
  }
  return SOCKET_VALID(result.return_value_);
}

Server::BootstrapExtensionPtr SocketInterfaceImpl::createBootstrapExtension(
    const Protobuf::Message& message, Server::Configuration::ServerFactoryContext& context) {
  auto config = MessageUtil::downcastAndValidate<
      const envoy::extensions::network::socket_interface::v3::IoUringSocketInterface&>(
      message, context.messageValidationContext().staticValidationVisitor());
  read_buffer_size_ =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, read_buffer_size, DefaultReadBufferSize);
  io_uring_factory_ = Io::ioUringFactory("envoy.extensions.io.io_uring");
  return std::make_unique<Network::SocketInterfaceExtension>(*this);
}

ProtobufTypes::MessagePtr SocketInterfaceImpl::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::network::socket_interface::v3::IoUringSocketInterface>();
}

REGISTER_FACTORY(SocketInterfaceImpl, Server::Configuration::BootstrapExtensionFactory);

} // namespace IoUring
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
