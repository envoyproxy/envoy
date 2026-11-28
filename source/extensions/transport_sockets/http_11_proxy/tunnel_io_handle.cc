#include "source/extensions/transport_sockets/http_11_proxy/tunnel_io_handle.h"

#include <algorithm>

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {

Api::IoCallUint64Result TunnelIoHandle::emptyReadResult() const {
  if (read_end_stream_) {
    // Peer closed: report EOF.
    return Api::ioCallUint64ResultNoError();
  }
  return {0, Network::IoSocketError::getIoSocketEagainError()};
}

Api::IoCallUint64Result TunnelIoHandle::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                              uint64_t num_slice) {
  if (closed_) {
    return closedResult();
  }
  if (inbound_.length() == 0) {
    return emptyReadResult();
  }
  uint64_t bytes_read = 0;
  for (uint64_t i = 0; i < num_slice && bytes_read < max_length && inbound_.length() > 0; ++i) {
    const uint64_t to_copy = std::min(
        {static_cast<uint64_t>(slices[i].len_), max_length - bytes_read, inbound_.length()});
    inbound_.copyOut(0, to_copy, slices[i].mem_);
    inbound_.drain(to_copy);
    bytes_read += to_copy;
  }
  return {bytes_read, Api::IoError::none()};
}

Api::IoCallUint64Result TunnelIoHandle::read(Buffer::Instance& buffer,
                                             std::optional<uint64_t> max_length) {
  if (closed_) {
    return closedResult();
  }
  if (inbound_.length() == 0) {
    return emptyReadResult();
  }
  const uint64_t to_move = std::min(max_length.value_or(inbound_.length()), inbound_.length());
  buffer.move(inbound_, to_move);
  return {to_move, Api::IoError::none()};
}

Api::IoCallUint64Result TunnelIoHandle::recv(void* buffer, size_t length, int flags) {
  if (closed_) {
    return closedResult();
  }
  if (inbound_.length() == 0) {
    return emptyReadResult();
  }
  const uint64_t to_copy = std::min(static_cast<uint64_t>(length), inbound_.length());
  inbound_.copyOut(0, to_copy, buffer);
  if (!(flags & MSG_PEEK)) {
    inbound_.drain(to_copy);
  }
  return {to_copy, Api::IoError::none()};
}

Api::IoCallUint64Result TunnelIoHandle::writev(const Buffer::RawSlice* slices, uint64_t num_slice) {
  if (closed_ || write_shutdown_) {
    return closedResult();
  }
  uint64_t bytes_written = 0;
  for (uint64_t i = 0; i < num_slice; ++i) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      outbound_.add(slices[i].mem_, slices[i].len_);
      bytes_written += slices[i].len_;
    }
  }
  return {bytes_written, Api::IoError::none()};
}

Api::IoCallUint64Result TunnelIoHandle::write(Buffer::Instance& buffer) {
  if (closed_ || write_shutdown_) {
    return closedResult();
  }
  const uint64_t length = buffer.length();
  outbound_.move(buffer);
  return {length, Api::IoError::none()};
}

Api::IoCallUint64Result TunnelIoHandle::send(const void* buffer, size_t length) {
  if (closed_ || write_shutdown_) {
    return closedResult();
  }
  outbound_.add(buffer, length);
  return {length, Api::IoError::none()};
}

Api::SysCallIntResult TunnelIoHandle::shutdown(int how) {
  if (how == ENVOY_SHUT_WR || how == ENVOY_SHUT_RDWR) {
    write_shutdown_ = true;
  }
  return {0, 0};
}

} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
