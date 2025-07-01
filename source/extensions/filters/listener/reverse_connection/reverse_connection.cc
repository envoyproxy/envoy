#include "source/extensions/filters/listener/reverse_connection/reverse_connection.h"

#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/network/listen_socket.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/reverse_connection/reverse_connection_utility.h"

// #include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ReverseConnection {

// Use centralized constants from utility
using ::Envoy::ReverseConnection::ReverseConnectionUtility;

Filter::Filter(const Config& config) : config_(config) {
  ENVOY_LOG(debug, "reverse_connection: ping_wait_timeout is {}",
            config_.pingWaitTimeout().count());
}

int Filter::fd() { return cb_->socket().ioHandle().fdDoNotUse(); }

Filter::~Filter() {
  ENVOY_LOG(debug,
            "reverse_connection: filter destroyed socket().isOpen(): {} connection_used_: {}",
            cb_->socket().isOpen(), connection_used_);
  // Only close the socket if the connection was not used (i.e., no data was received)
  // If connection_used_ is true, Envoy needs the socket for the new connection
  if (!connection_used_ && cb_->socket().isOpen()) {
    ENVOY_LOG(debug, "reverse_connection: closing unused socket in destructor, fd {}", fd());
    cb_->socket().close();
  }
}

void Filter::onClose() {
  ENVOY_LOG(debug, "reverse_connection: close");

  const std::string& connectionKey =
      cb_->socket().connectionInfoProvider().localAddress()->asString();

  ENVOY_LOG(debug, "reverse_connection: onClose: connectionKey: {} connection_used_ {}",
            connectionKey, connection_used_);

  // If a connection is closed before data is received, mark the socket dead.
  if (!connection_used_) {
    ENVOY_LOG(debug, "reverse_connection: marking the socket dead, fd {}", fd());
    cb_->socket().ioHandle().close();
  }
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "reverse_connection: New connection accepted");
  connection_used_ = false;
  cb_ = &cb;
  ping_wait_timer_ = cb.dispatcher().createTimer([this]() { onPingWaitTimeout(); });
  ping_wait_timer_->enableTimer(config_.pingWaitTimeout());

  // Wait for data.
  return Network::FilterStatus::StopIteration;
}

size_t Filter::maxReadBytes() const { return ReverseConnectionUtility::PING_MESSAGE.length(); }

void Filter::onPingWaitTimeout() {
  ENVOY_LOG(debug, "reverse_connection: timed out waiting for ping request");
  const std::string& connectionKey =
      cb_->socket().connectionInfoProvider().localAddress()->asString();
  ENVOY_LOG(debug,
            "Connection socket FD: {} local address: {} remote address: {} closed; Reporting to "
            "RCManager.",
            fd(), connectionKey,
            cb_->socket().connectionInfoProvider().remoteAddress()->asStringView());

  // Connection timed out waiting for data - close and continue filter chain

  cb_->continueFilterChain(false);
}

Network::FilterStatus Filter::onData(Network::ListenerFilterBuffer& buffer) {
  const ReadOrParseState read_state = parseBuffer(buffer);
  switch (read_state) {
  case ReadOrParseState::Error:
    return Network::FilterStatus::StopIteration;
  case ReadOrParseState::TryAgainLater:
    return Network::FilterStatus::StopIteration;
  case ReadOrParseState::Done:
    ENVOY_LOG(debug, "reverse_connection: marking the socket ready for use, fd {}", fd());
    // Mark the connection as used and continue with normal processing
    const std::string& connectionKey =
        cb_->socket().connectionInfoProvider().localAddress()->asString();
    ENVOY_LOG(debug, "reverse_connection: marking the socket ready for use, connectionKey: {}",
              connectionKey);
    connection_used_ = true;
    return Network::FilterStatus::Continue;
  }
  return Network::FilterStatus::Continue;
}

ReadOrParseState Filter::parseBuffer(Network::ListenerFilterBuffer& buffer) {
  auto raw_slice = buffer.rawSlice();
  auto buf = absl::string_view(static_cast<const char*>(raw_slice.mem_), raw_slice.len_);

  ENVOY_LOG(debug, "reverse_connection: Data received, len: {}", buf.length());
  if (buf.length() == 0) {
    // Remote closed.
    return ReadOrParseState::Error;
  }

  // Use utility to check for RPING messages (raw or HTTP-embedded)
  if (ReverseConnectionUtility::isPingMessage(buf)) {
    ENVOY_LOG(debug, "reverse_connection: Received RPING msg on fd {}", fd());

    if (!buffer.drain(buf.length())) {
      ENVOY_LOG(error, "reverse_connection: could not drain buffer for ping message");
    }

    // Use utility to send RPING response
    const Api::IoCallUint64Result write_result =
        ReverseConnectionUtility::sendPingResponse(cb_->socket().ioHandle());

    if (write_result.ok()) {
      ENVOY_LOG(trace, "reverse_connection: fd {} sent ping response, bytes: {}", fd(),
                write_result.return_value_);
    } else {
      ENVOY_LOG(trace, "reverse_connection: fd {} failed to send ping response, error: {}", fd(),
                write_result.err_->getErrorDetails());
    }

    ping_wait_timer_->enableTimer(config_.pingWaitTimeout());
    // Return a status to wait for data.
    return ReadOrParseState::TryAgainLater;
  }

  ENVOY_LOG(debug, "reverse_connection: fd {} received data, stopping RPINGs", fd());
  return ReadOrParseState::Done;
}

} // namespace ReverseConnection
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
