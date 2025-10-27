#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_io_handle.h"

#include "source/common/common/logger.h"
#include "source/common/network/socket_interface.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

UpstreamReverseConnectionIOHandle::UpstreamReverseConnectionIOHandle(
    Network::ConnectionSocketPtr socket, const std::string& cluster_name)
    : IoSocketHandleImpl(socket->ioHandle().fdDoNotUse()), cluster_name_(cluster_name),
      owned_socket_(std::move(socket)) {
  ENVOY_LOG(trace, "reverse_tunnel: created IO handle for cluster: {}, fd: {}", cluster_name_, fd_);
}

UpstreamReverseConnectionIOHandle::~UpstreamReverseConnectionIOHandle() {
  ENVOY_LOG(trace, "reverse_tunnel: destroying IO handle for cluster: {}, fd: {}", cluster_name_,
            fd_);
}

Api::SysCallIntResult
UpstreamReverseConnectionIOHandle::connect(Network::Address::InstanceConstSharedPtr address) {
  ENVOY_LOG(trace, "reverse_tunnel: connect() to {} - connection already established",
            address->asString());
  return Api::SysCallIntResult{0, 0};
}

Api::IoCallUint64Result UpstreamReverseConnectionIOHandle::close() {
  ENVOY_LOG(debug, "reverse_tunnel: close() called for fd: {}", fd_);

  if (owned_socket_) {
    ENVOY_LOG(debug, "reverse_tunnel: releasing socket for cluster: {}", cluster_name_);

    // Before releasing the socket, notify the socket manager that this socket is dying.
    // This will trigger the callback chain to mark the host as unhealthy.
    auto* upstream_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    if (upstream_interface == nullptr) {
      ENVOY_LOG(error, "reverse_tunnel: upstream_interface is null");
      return IoSocketHandleImpl::close();
    }
    auto* acceptor = const_cast<ReverseTunnelAcceptor*>(
        dynamic_cast<const ReverseTunnelAcceptor*>(upstream_interface));
    if (acceptor == nullptr) {
      ENVOY_LOG(error, "reverse_tunnel: acceptor is null after dynamic_cast");
      return IoSocketHandleImpl::close();
    }
    auto* tls_registry = acceptor->getLocalRegistry();
    if (tls_registry == nullptr || tls_registry->socketManager() == nullptr) {
      ENVOY_LOG(error,
                "reverse_tunnel: tls_registry or socketManager is null. tls_registry={}, "
                "socketManager={}",
                tls_registry ? "not_null" : "null",
                tls_registry && tls_registry->socketManager() ? "not_null" : "null");
      return IoSocketHandleImpl::close();
    }
    ENVOY_LOG(warn, "reverse_tunnel: notifying socket manager of socket death. fd: {}", fd_);
    auto* socket_manager = tls_registry->socketManager();
    socket_manager->markSocketDead(fd_);

    owned_socket_.reset();
    SET_SOCKET_INVALID(fd_);
    return Api::ioCallUint64ResultNoError();
  }
  return IoSocketHandleImpl::close();
}

Api::SysCallIntResult UpstreamReverseConnectionIOHandle::shutdown(int how) {
  ENVOY_LOG(trace, "reverse_tunnel: shutdown({}) called for fd: {}", how, fd_);
  // If we still own the socket, ignore shutdown to avoid affecting a socket that will be
  // handed over to the upstream connection.
  if (owned_socket_) {
    ENVOY_LOG(debug, "reverse_tunnel: ignoring shutdown() call for owned socket fd: {}", fd_);
    return Api::SysCallIntResult{0, 0};
  }
  return IoSocketHandleImpl::shutdown(how);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
