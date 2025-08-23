#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"

#include <string>

#include "source/common/common/logger.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// UpstreamReverseConnectionIOHandle implementation
UpstreamReverseConnectionIOHandle::UpstreamReverseConnectionIOHandle(
    Network::ConnectionSocketPtr socket, const std::string& cluster_name)
    : IoSocketHandleImpl(socket->ioHandle().fdDoNotUse()), cluster_name_(cluster_name),
      owned_socket_(std::move(socket)) {

  ENVOY_LOG(trace, "reverse_tunnel: created IO handle for cluster: {}, fd: {}", cluster_name_, fd_);
}

UpstreamReverseConnectionIOHandle::~UpstreamReverseConnectionIOHandle() {
  ENVOY_LOG(trace, "reverse_tunnel: destroying IO handle for cluster: {}, fd: {}", cluster_name_,
            fd_);
  // The owned_socket_ will be automatically destroyed via RAII.
}

Api::SysCallIntResult UpstreamReverseConnectionIOHandle::connect(
    Envoy::Network::Address::InstanceConstSharedPtr address) {
  ENVOY_LOG(trace, "reverse_tunnel: connect() to {} - connection already established",
            address->asString());

  // For reverse connections, the connection is already established.
  return Api::SysCallIntResult{0, 0};
}

Api::IoCallUint64Result UpstreamReverseConnectionIOHandle::close() {
  ENVOY_LOG(debug, "reverse_tunnel: close() called for fd: {}", fd_);

  // Prefer letting the owned ConnectionSocket perform the actual close to avoid
  // double-close.
  if (owned_socket_) {
    ENVOY_LOG(debug, "reverse_tunnel: releasing socket for cluster: {}", cluster_name_);
    owned_socket_.reset();
    // Invalidate our fd so base destructor won't close again.
    SET_SOCKET_INVALID(fd_);
    return Api::ioCallUint64ResultNoError();
  }
  // If we no longer own the socket, fall back to base close.
  return IoSocketHandleImpl::close();
}

// ReverseTunnelAcceptor implementation
ReverseTunnelAcceptor::ReverseTunnelAcceptor(Server::Configuration::ServerFactoryContext& context)
    : extension_(nullptr), context_(&context) {
  ENVOY_LOG(debug, "reverse_tunnel: created acceptor");
}

Envoy::Network::IoHandlePtr
ReverseTunnelAcceptor::socket(Envoy::Network::Socket::Type, Envoy::Network::Address::Type,
                              Envoy::Network::Address::IpVersion, bool,
                              const Envoy::Network::SocketCreationOptions&) const {

  ENVOY_LOG(warn, "reverse_tunnel: socket() called without address - returning nullptr");

  // Reverse connection sockets should always have an address.
  return nullptr;
}

Envoy::Network::IoHandlePtr
ReverseTunnelAcceptor::socket(Envoy::Network::Socket::Type socket_type,
                              const Envoy::Network::Address::InstanceConstSharedPtr addr,
                              const Envoy::Network::SocketCreationOptions& options) const {
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
      ENVOY_LOG(info, "reverse_tunnel: reusing cached socket for node: {}", node_id);
      // Create IOHandle that owns the socket using RAII.
      auto io_handle =
          std::make_unique<UpstreamReverseConnectionIOHandle>(std::move(socket), node_id);
      return io_handle;
    }
  }

  // No sockets available, fallback to standard socket interface.
  ENVOY_LOG(debug, "reverse_tunnel: no available connection, falling back to standard socket");
  // Emit a counter to aid diagnostics in NAT scenarios where direct connect will fail.
  if (extension_) {
    auto& scope = extension_->getStatsScope();
    std::string counter_name =
        fmt::format("{}.fallback_no_reverse_socket", extension_->statPrefix());
    Stats::StatNameManagedStorage counter_name_storage(counter_name, scope.symbolTable());
    auto& counter = scope.counterFromStatName(counter_name_storage.statName());
    counter.inc();
  }
  return Network::socketInterface(
             "envoy.extensions.network.socket_interface.default_socket_interface")
      ->socket(socket_type, addr, options);
}

bool ReverseTunnelAcceptor::ipFamilySupported(int domain) {
  return domain == AF_INET || domain == AF_INET6;
}

// Get thread local registry for the current thread
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
