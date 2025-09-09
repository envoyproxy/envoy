#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// DownstreamReverseConnectionIOHandle constructor implementation
DownstreamReverseConnectionIOHandle::DownstreamReverseConnectionIOHandle(
    Network::ConnectionSocketPtr socket, ReverseConnectionIOHandle* parent,
    const std::string& connection_key)
    : IoSocketHandleImpl(socket->ioHandle().fdDoNotUse()), owned_socket_(std::move(socket)),
      parent_(parent), connection_key_(connection_key) {
  ENVOY_LOG(debug,
            "DownstreamReverseConnectionIOHandle: taking ownership of socket with FD: {} for "
            "connection key: {}",
            fd_, connection_key_);
}

// DownstreamReverseConnectionIOHandle destructor implementation
DownstreamReverseConnectionIOHandle::~DownstreamReverseConnectionIOHandle() {
  ENVOY_LOG(
      debug,
      "DownstreamReverseConnectionIOHandle: destroying handle for FD: {} with connection key: {}",
      fd_, connection_key_);
}

// DownstreamReverseConnectionIOHandle close() implementation.
Api::IoCallUint64Result DownstreamReverseConnectionIOHandle::close() {
  ENVOY_LOG(
      debug,
      "DownstreamReverseConnectionIOHandle: closing handle for FD: {} with connection key: {}", fd_,
      connection_key_);

  // If we're ignoring close calls during socket hand-off, just return success.
  if (ignore_close_and_shutdown_) {
    ENVOY_LOG(
        debug,
        "DownstreamReverseConnectionIOHandle: ignoring close() call during socket hand-off for "
        "connection key: {}",
        connection_key_);
    return Api::ioCallUint64ResultNoError();
  }

  // Prevent double-closing by checking if already closed
  if (fd_ < 0) {
    ENVOY_LOG(debug,
              "DownstreamReverseConnectionIOHandle: handle already closed for connection key: {}",
              connection_key_);
    return Api::ioCallUint64ResultNoError();
  }

  // Notify parent that this downstream connection has been closed
  // This will trigger re-initiation of the reverse connection if needed.
  if (parent_) {
    parent_->onDownstreamConnectionClosed(connection_key_);
    ENVOY_LOG(
        debug,
        "DownstreamReverseConnectionIOHandle: notified parent of connection closure for key: {}",
        connection_key_);
  }

  // Reset the owned socket to properly close the connection.
  if (owned_socket_) {
    owned_socket_.reset();
  }
  return IoSocketHandleImpl::close();
}

// DownstreamReverseConnectionIOHandle shutdown() implementation.
Api::SysCallIntResult DownstreamReverseConnectionIOHandle::shutdown(int how) {
  ENVOY_LOG(trace,
            "DownstreamReverseConnectionIOHandle: shutdown({}) called for FD: {} with connection "
            "key: {}",
            how, fd_, connection_key_);

  // If we're ignoring shutdown calls during socket hand-off, just return success.
  if (ignore_close_and_shutdown_) {
    ENVOY_LOG(
        debug,
        "DownstreamReverseConnectionIOHandle: ignoring shutdown() call during socket hand-off "
        "for connection key: {}",
        connection_key_);
    return Api::SysCallIntResult{0, 0};
  }

  return IoSocketHandleImpl::shutdown(how);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
