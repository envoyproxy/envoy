#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"

#include <string>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

namespace {
// IoHandle whose connect() fails immediately with ECONNREFUSED and issues no syscall. The acceptor
// returns one on a pool miss so ConnectionImpl::connect() takes its immediate-error path and raises
// RemoteClose, exactly as a refused loopback connect would but without the loopback syscalls.
class PoolMissIoHandle : public Network::IoSocketHandleImpl {
public:
  using Network::IoSocketHandleImpl::IoSocketHandleImpl;

  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr) override {
    return {-1, ECONNREFUSED};
  }
};
} // namespace

// ReverseTunnelAcceptor implementation
ReverseTunnelAcceptor::ReverseTunnelAcceptor(Server::Configuration::ServerFactoryContext& context)
    : context_(&context) {
  ENVOY_LOG(debug, "reverse_tunnel: created acceptor");
}

Envoy::Network::IoHandlePtr
ReverseTunnelAcceptor::socket(Envoy::Network::Socket::Type, Envoy::Network::Address::Type,
                              Envoy::Network::Address::IpVersion, bool,
                              const Envoy::Network::SocketCreationOptions&) const {

  ENVOY_LOG(warn, "reverse_tunnel: socket() called without address; returning nullptr");

  // Reverse connection sockets should always have an address.
  return nullptr;
}

Envoy::Network::IoHandlePtr
ReverseTunnelAcceptor::socket(Envoy::Network::Socket::Type,
                              const Envoy::Network::Address::InstanceConstSharedPtr addr,
                              const Envoy::Network::SocketCreationOptions&) const {
  ENVOY_LOG(debug, "reverse_tunnel: socket() called for address: {}, node: {}", addr->asString(),
            addr->logicalName());

  // For upstream reverse connections, we need to get the thread-local socket manager
  // and check if there are any cached connections available
  auto* tls_registry = getLocalRegistry();
  if (tls_registry && tls_registry->socketManager()) {
    ENVOY_LOG(trace, "reverse_tunnel: running on dispatcher: {}",
              tls_registry->dispatcher().name());
    auto* socket_manager = tls_registry->socketManager();

    // The address's logical name is the node ID.
    std::string node_id = addr->logicalName();
    ENVOY_LOG(debug, "reverse_tunnel: using node_id: {}", node_id);

    // Try to get a cached socket for the node.
    auto socket = socket_manager->getConnectionSocket(node_id);
    if (socket) {
      ENVOY_LOG(debug, "reverse_tunnel: reusing cached socket for node: {}", node_id);
      // Create IOHandle that owns the socket using RAII.
      auto io_handle = std::make_unique<UpstreamReverseConnectionIOHandle>(std::move(socket),
                                                                           node_id, *tls_registry);
      return io_handle;
    }
  }

  // No cached reverse tunnel for this node. Record the miss and return a socket whose connect()
  // fails immediately, so the request fails through the normal refused-connection path without
  // dialing the synthetic loopback address.
  ENVOY_LOG(debug,
            "reverse_tunnel: no available connection for node {}, returning a failing socket",
            addr->logicalName());
  if (extension_ != nullptr) {
    extension_->incPoolMiss();
  }

  // ConnectionImpl raises IS_ENVOY_BUG on a closed fd, so the returned handle must own an open
  // socket even though its connect() never issues a syscall. SOCK_NONBLOCK and SOCK_CLOEXEC are not
  // portable socket() flags, so request them only where supported and set non-blocking explicitly
  // otherwise, mirroring the default socket interface.
#if defined(__APPLE__) || defined(WIN32)
  const int socket_flags = SOCK_STREAM;
#else
  const int socket_flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
  const Api::SysCallSocketResult socket_result =
      Api::OsSysCallsSingleton::get().socket(AF_INET, socket_flags, 0);
  if (SOCKET_INVALID(socket_result.return_value_)) {
    ENVOY_LOG(error, "reverse_tunnel: failed to create pool-miss socket: {}",
              errorDetails(socket_result.errno_));
    return nullptr;
  }
  // Record the socket domain so address-family-aware socket options select the IPv4 variant.
  auto io_handle = std::make_unique<PoolMissIoHandle>(socket_result.return_value_,
                                                      /*socket_v6only=*/false, /*domain=*/AF_INET);
#if defined(__APPLE__) || defined(WIN32)
  io_handle->setBlocking(false);
#endif
  return io_handle;
}

bool ReverseTunnelAcceptor::ipFamilySupported(int domain) {
  return domain == AF_INET || domain == AF_INET6;
}

// Get thread local registry for the current thread.
UpstreamSocketThreadLocal* ReverseTunnelAcceptor::getLocalRegistry() const {
  if (extension_) {
    return extension_->getLocalRegistry();
  }
  return nullptr;
}

// BootstrapExtensionFactory
Server::BootstrapExtensionPtr ReverseTunnelAcceptor::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  ENVOY_LOG(debug, "ReverseTunnelAcceptor::createBootstrapExtension()");
  // Cast the config to the proper type.
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
          UpstreamReverseConnectionSocketInterface&>(config, context.messageValidationVisitor());

  // Set the context for this socket interface instance.
  context_ = &context;

  // Return a SocketInterfaceExtension that wraps this socket interface.
  return std::make_unique<ReverseTunnelAcceptorExtension>(*this, context, message);
}

ProtobufTypes::MessagePtr ReverseTunnelAcceptor::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::
                              v3::UpstreamReverseConnectionSocketInterface>();
}

REGISTER_FACTORY(ReverseTunnelAcceptor, Server::Configuration::BootstrapExtensionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
