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

// #include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ReverseConnection {

const absl::string_view Filter::RPING_MSG = "RPING";
const absl::string_view Filter::PROXY_MSG = "PROXY";

Filter::Filter(const Config& config) : config_(config) {
  ENVOY_LOG(debug, "reverse_connection: ping_wait_timeout is {}",
            config_.pingWaitTimeout().count());
}

int Filter::fd() { return cb_->socket().ioHandle().fdDoNotUse(); }

Filter::~Filter() {}

void Filter::onClose() {
  ENVOY_LOG(debug, "reverse_connection: close");

  const std::string& connectionKey =
      cb_->socket().connectionInfoProvider().localAddress()->asString();

  // The rc filter responds to pings until data is received, so if onClose() is invoked,
  // then an idle reverse connection has been closed.
  cb_->dispatcher().connectionHandler()->reverseConnRegistry().getRCManager().notifyConnectionClose(
      connectionKey, false /* is_used */);
  // Marking the connection as used here to avoid marking the socket dead again in the destructor.
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "reverse_connection: New connection accepted");
  cb_ = &cb;
  ping_wait_timer_ = cb.dispatcher().createTimer([this]() { onPingWaitTimeout(); });
  ping_wait_timer_->enableTimer(config_.pingWaitTimeout());

  // Wait for data.
  return Network::FilterStatus::StopIteration;
}

size_t Filter::maxReadBytes() const { return RPING_MSG.length(); }

void Filter::onPingWaitTimeout() {
  ENVOY_LOG(debug, "reverse_connection: timed out waiting for ping request");
  const std::string& connectionKey =
      cb_->socket().connectionInfoProvider().localAddress()->asString();
  ENVOY_LOG(debug,
            "Connection socket FD: {} local address: {} remote address: {} closed; Reporting to "
            "RCManager.",
            fd(), connectionKey,
            cb_->socket().connectionInfoProvider().remoteAddress()->asStringView());
  cb_->dispatcher().connectionHandler()->reverseConnRegistry().getRCManager().notifyConnectionClose(
      connectionKey, false);
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
    // Call the RC Manager to update the RCManager Stats and log the connection used.
    const std::string& connectionKey =
        cb_->socket().connectionInfoProvider().localAddress()->asString();
    cb_->dispatcher().connectionHandler()->reverseConnRegistry().getRCManager().markConnUsed(
        connectionKey);
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

  // We will compare the received bytes with the expected "RPING" msg. If,
  // we found that the received bytes are not "RPING", this means, that peer
  // socket is assigned to an upstream cluster. Otherwise, we will send "RPING"
  // as a response.
  if (!memcmp(buf.data(), RPING_MSG.data(), RPING_MSG.length())) {
    ENVOY_LOG(debug, "reverse_connection: Revceived {} msg on fd {}", RPING_MSG, fd());
    if (!buffer.drain(RPING_MSG.length())) {
      ENVOY_LOG(error, "reverse_connection: could not drain buffer for ping message");
    }

    // Echo the RPING message back.
    Buffer::OwnedImpl rping_buf(RPING_MSG);
    const Api::IoCallUint64Result write_result = cb_->socket().ioHandle().write(rping_buf);
    if (write_result.ok()) {
      ENVOY_LOG(trace, "reverse_connection: fd {} send ping response rc:{}", fd(),
                write_result.return_value_);
    } else {
      ENVOY_LOG(trace, "reverse_connection: fd {} send ping response rc:{} errno {}", fd(),
                write_result.return_value_, write_result.err_->getErrorDetails());
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
