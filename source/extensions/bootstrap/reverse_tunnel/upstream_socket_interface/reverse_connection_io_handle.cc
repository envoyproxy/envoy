#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_io_handle.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

UpstreamReverseConnectionIOHandle::UpstreamReverseConnectionIOHandle(
    Network::ConnectionSocketPtr socket, const std::string& cluster_name,
    ReverseTunnelAcceptor* acceptor)
    : IoSocketHandleImpl(socket->ioHandle().fdDoNotUse()), cluster_name_(cluster_name),
      owned_socket_(std::move(socket)), acceptor_(acceptor) {
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
    if (acceptor_ != nullptr) {
      ENVOY_LOG(debug, "reverse_tunnel: got acceptor");
      UpstreamSocketThreadLocal* tls_registry = acceptor_->getLocalRegistry();
      if (tls_registry != nullptr && tls_registry->socketManager() != nullptr) {
        UpstreamSocketManager* socket_manager = tls_registry->socketManager();
        ENVOY_LOG(debug,
                  "reverse_tunnel: notifying socket manager of socket death. fd: {}",
                  fd_);
        socket_manager->markSocketDead(fd_);
        ENVOY_LOG(debug, "reverse_tunnel: markSocketDead() completed for fd: {}", fd_);
      } else {
        ENVOY_LOG(debug,
                  "reverse_tunnel: could not get socket manager to mark socket dead. fd: {}, tls_registry={}, socketManager={}",
                  fd_, tls_registry ? "not_null" : "null",
                  (tls_registry && tls_registry->socketManager()) ? "not_null" : "null");
      }
    } else {
      ENVOY_LOG(debug, "reverse_tunnel: acceptor is null");
    }

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
