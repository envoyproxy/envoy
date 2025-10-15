#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "envoy/network/address.h"
#include "envoy/registry/registry.h"

#include "source/common/common/logger.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_address.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// ReverseTunnelInitiator implementation
ReverseTunnelInitiator::ReverseTunnelInitiator(Server::Configuration::ServerFactoryContext& context)
    : extension_(nullptr), context_(&context) {
  ENVOY_LOG(debug, "Created ReverseTunnelInitiator.");
}

DownstreamSocketThreadLocal* ReverseTunnelInitiator::getLocalRegistry() const {
  if (!extension_ || !extension_->getLocalRegistry()) {
    return nullptr;
  }
  return extension_->getLocalRegistry();
}

Envoy::Network::IoHandlePtr
ReverseTunnelInitiator::socket(Envoy::Network::Socket::Type socket_type,
                               Envoy::Network::Address::Type addr_type,
                               Envoy::Network::Address::IpVersion version, bool,
                               const Envoy::Network::SocketCreationOptions&) const {
  ENVOY_LOG(debug, "ReverseTunnelInitiator: type={}, addr_type={}", static_cast<int>(socket_type),
            static_cast<int>(addr_type));

  // This method is called without reverse connection config, so create a regular socket.
  int domain;
  if (addr_type == Envoy::Network::Address::Type::Ip) {
    domain = (version == Envoy::Network::Address::IpVersion::v4) ? AF_INET : AF_INET6;
  } else {
    // For pipe addresses.
    domain = AF_UNIX;
  }
  int sock_type = (socket_type == Envoy::Network::Socket::Type::Stream) ? SOCK_STREAM : SOCK_DGRAM;
  int sock_fd = ::socket(domain, sock_type, 0);
  if (sock_fd == -1) {
    ENVOY_LOG(error, "Failed to create fallback socket: {}", errorDetails(errno));
    return nullptr;
  }
  return std::make_unique<Envoy::Network::IoSocketHandleImpl>(sock_fd);
}

/**
 * Thread-safe helper method to create reverse connection socket with config.
 */
Envoy::Network::IoHandlePtr ReverseTunnelInitiator::createReverseConnectionSocket(
    Envoy::Network::Socket::Type socket_type, Envoy::Network::Address::Type addr_type,
    Envoy::Network::Address::IpVersion version, const ReverseConnectionSocketConfig& config) const {

  // Return early if no remote clusters are configured
  if (config.remote_clusters.empty()) {
    ENVOY_LOG(debug, "ReverseTunnelInitiator: No remote clusters configured, returning nullptr");
    return nullptr;
  }

  ENVOY_LOG(debug, "ReverseTunnelInitiator: Creating reverse connection socket for cluster: {}",
            config.remote_clusters[0].cluster_name);

  // For stream sockets on IP addresses, create our reverse connection IOHandle.
  if (socket_type == Envoy::Network::Socket::Type::Stream &&
      addr_type == Envoy::Network::Address::Type::Ip) {
    // Create socket file descriptor using system calls.
    int domain = (version == Envoy::Network::Address::IpVersion::v4) ? AF_INET : AF_INET6;
    int sock_fd = ::socket(domain, SOCK_STREAM, 0);
    if (sock_fd == -1) {
      ENVOY_LOG(error, "Failed to create socket: {}", errorDetails(errno));
      return nullptr;
    }

    ENVOY_LOG(
        debug,
        "ReverseTunnelInitiator: Created socket fd={}, wrapping with ReverseConnectionIOHandle",
        sock_fd);

    // Get the scope from thread local registry, fallback to context scope
    Stats::Scope* scope_ptr = &context_->scope();
    auto* tls_registry = getLocalRegistry();
    if (tls_registry) {
      scope_ptr = &tls_registry->scope();
    }

    // Create ReverseConnectionIOHandle with cluster manager from context and scope.
    return std::make_unique<ReverseConnectionIOHandle>(sock_fd, config, context_->clusterManager(),
                                                       extension_, *scope_ptr);
  }

  // Fall back to regular socket for non-stream or non-IP sockets.
  return socket(socket_type, addr_type, version, false, Envoy::Network::SocketCreationOptions{});
}

Envoy::Network::IoHandlePtr
ReverseTunnelInitiator::socket(Envoy::Network::Socket::Type socket_type,
                               const Envoy::Network::Address::InstanceConstSharedPtr addr,
                               const Envoy::Network::SocketCreationOptions& options) const {

  // Extract reverse connection configuration from address.
  const auto* reverse_addr = dynamic_cast<const ReverseConnectionAddress*>(addr.get());
  if (reverse_addr) {
    // Get the reverse connection config from the address.
    ENVOY_LOG(debug, "ReverseTunnelInitiator: reverse_addr: {}", reverse_addr->asString());
    const auto& config = reverse_addr->reverseConnectionConfig();

    // Convert ReverseConnectionAddress::ReverseConnectionConfig to ReverseConnectionSocketConfig.
    ReverseConnectionSocketConfig socket_config;
    socket_config.src_node_id = config.src_node_id;
    socket_config.src_cluster_id = config.src_cluster_id;
    socket_config.src_tenant_id = config.src_tenant_id;

    // Add the remote cluster configuration.
    RemoteClusterConnectionConfig cluster_config(config.remote_cluster, config.connection_count);
    socket_config.remote_clusters.push_back(cluster_config);

    // Pass config directly to helper method.
    return createReverseConnectionSocket(
        socket_type, addr->type(),
        addr->ip() ? addr->ip()->version() : Envoy::Network::Address::IpVersion::v4, socket_config);
  }

  // Delegate to the other socket() method for non-reverse-connection addresses.
  return socket(socket_type, addr->type(),
                addr->ip() ? addr->ip()->version() : Envoy::Network::Address::IpVersion::v4, false,
                options);
}

bool ReverseTunnelInitiator::ipFamilySupported(int domain) {
  return domain == AF_INET || domain == AF_INET6;
}

Server::BootstrapExtensionPtr ReverseTunnelInitiator::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  ENVOY_LOG(debug, "ReverseTunnelInitiator::createBootstrapExtension()");
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
          DownstreamReverseConnectionSocketInterface&>(config, context.messageValidationVisitor());
  context_ = &context;
  // Create the bootstrap extension and store reference to it.
  auto extension = std::make_unique<ReverseTunnelInitiatorExtension>(context, message);
  extension_ = extension.get();
  return extension;
}

ProtobufTypes::MessagePtr ReverseTunnelInitiator::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
          DownstreamReverseConnectionSocketInterface>();
}

REGISTER_FACTORY(ReverseTunnelInitiator, Server::Configuration::BootstrapExtensionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
