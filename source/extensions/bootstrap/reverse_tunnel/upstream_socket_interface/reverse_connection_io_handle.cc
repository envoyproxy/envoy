#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_io_handle.h"

#include "source/common/common/logger.h"

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
