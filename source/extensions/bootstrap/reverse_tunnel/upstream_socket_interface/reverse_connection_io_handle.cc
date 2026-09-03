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
    Network::ConnectionSocketPtr socket, const std::string& cluster_name,
    UpstreamSocketThreadLocal& registry)
    : IoSocketHandleImpl(socket->ioHandle().fdDoNotUse()), cluster_name_(cluster_name),
      owned_socket_(std::move(socket)), registry_{registry},
      cx_post_upgrade_lifetime_{*registry.cx_post_upgrade_lifetime_,
                                registry.dispatcher().timeSource()} {
  ENVOY_LOG(trace, "reverse_tunnel: created IO handle for cluster: {}, fd: {}", cluster_name_, fd_);
}

UpstreamReverseConnectionIOHandle::~UpstreamReverseConnectionIOHandle() {
  ENVOY_LOG(trace, "reverse_tunnel: destroying IO handle for cluster: {}, fd: {}", cluster_name_,
            fd_);
  cx_post_upgrade_lifetime_.complete();
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

    auto* socket_manager = registry_.socketManager();
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
