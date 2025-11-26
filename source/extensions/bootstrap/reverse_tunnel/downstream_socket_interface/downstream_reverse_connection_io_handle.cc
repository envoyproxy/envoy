#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"

#include "absl/strings/string_view.h"

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

Api::IoCallUint64Result
DownstreamReverseConnectionIOHandle::read(Buffer::Instance& buffer,
                                          absl::optional<uint64_t> max_length) {
  // Perform the actual read first.
  Api::IoCallUint64Result result = IoSocketHandleImpl::read(buffer, max_length);
  ENVOY_LOG(trace, "DownstreamReverseConnectionIOHandle: read result: {}", result.return_value_);

  // If RPING keepalives are still active, check whether the incoming data is a RPING message.
  if (ping_echo_active_ && result.err_ == nullptr && result.return_value_ > 0) {
    const uint64_t expected =
        ::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::PING_MESSAGE
            .size();

    // Compare up to the expected size using a zero-copy view.
    const uint64_t len = std::min<uint64_t>(buffer.length(), expected);
    const char* data = static_cast<const char*>(buffer.linearize(len));
    absl::string_view peek_sv{data, static_cast<size_t>(len)};

    // Check if we have a complete RPING message.
    if (len == expected &&
        ::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::isPingMessage(
            peek_sv)) {
      // Found a complete RPING. Echo and drain it from the buffer.
      buffer.drain(expected);
      auto echo_rc = ::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
          sendPingResponse(*this);
      if (!echo_rc.ok()) {
        ENVOY_LOG(trace, "DownstreamReverseConnectionIOHandle: failed to send RPING echo on FD: {}",
                  fd_);
      } else {
        ENVOY_LOG(trace, "DownstreamReverseConnectionIOHandle: echoed RPING on FD: {}", fd_);
      }

      // If buffer only contained RPING, return showing we processed it.
      if (buffer.length() == 0) {
        return Api::IoCallUint64Result{expected, Api::IoError::none()};
      }

      // RPING followed by application data. Disable echo and return the remaining data.
      ENVOY_LOG(trace,
                "DownstreamReverseConnectionIOHandle: received application data after RPING, "
                "disabling RPING echo for FD: {}",
                fd_);
      ping_echo_active_ = false;
      // The adjusted return value is the number of bytes excluding the drained RPING. It should be
      // transparent to upper layers that the RPING was processed.
      const uint64_t adjusted =
          (result.return_value_ >= expected) ? (result.return_value_ - expected) : 0;
      return Api::IoCallUint64Result{adjusted, Api::IoError::none()};
    }

    // If partial data could be the start of RPING (only when fewer than expected bytes).
    if (len < expected) {
      const absl::string_view rping_prefix =
          ReverseConnectionUtility::PING_MESSAGE.substr(0, static_cast<size_t>(len));
      if (peek_sv == rping_prefix) {
        ENVOY_LOG(trace,
                  "DownstreamReverseConnectionIOHandle: partial RPING received ({} bytes), waiting "
                  "for more.",
                  len);
        return result; // Wait for more data.
      }
    }

    // Data is not RPING (complete or partial). Disable echo permanently.
    ENVOY_LOG(trace,
              "DownstreamReverseConnectionIOHandle: received application data ({} bytes), "
              "disabling RPING echo for FD: {}",
              len, fd_);
    ping_echo_active_ = false;
  }

  return result;
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

  // Prevent double-closing by checking if already closed.
  if (fd_ < 0) {
    ENVOY_LOG(debug,
              "DownstreamReverseConnectionIOHandle: handle already closed for connection key: {}",
              connection_key_);
    return Api::ioCallUint64ResultNoError();
  }

  // Notify the parent that this downstream connection has been closed.
  // This can trigger re-initiation of the reverse connection if needed.
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

Api::SysCallIntResult DownstreamReverseConnectionIOHandle::shutdown(int how) {
  ENVOY_LOG(trace,
            "DownstreamReverseConnectionIOHandle: shutdown({}) called for FD: {} with connection "
            "key: {}",
            how, fd_, connection_key_);

  // If shutdown is ignored during socket hand-off, return success.
  if (ignore_close_and_shutdown_) {
    ENVOY_LOG(
        debug,
        "DownstreamReverseConnectionIOHandle: ignoring shutdown() call during socket hand-off for "
        "connection key: {}",
        connection_key_);
    return Api::SysCallIntResult{0, 0};
  }

  return IoSocketHandleImpl::shutdown(how);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
