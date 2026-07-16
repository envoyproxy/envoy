#include "source/extensions/bootstrap/reverse_tunnel/common/rping_interceptor.h"

#include <cstring>
#include <string>
#include <vector>

#include "source/common/network/io_socket_error_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

Api::IoCallUint64Result RpingInterceptor::read(Buffer::Instance& buffer,
                                               std::optional<uint64_t> max_length) {
  // Guard the inner readv() dispatch to a passthrough so RPING is detected once, here.
  processing_read_ = true;
  Api::IoCallUint64Result result = IoSocketHandleImpl::read(buffer, max_length);
  processing_read_ = false;
  ENVOY_LOG(trace, "RpingInterceptor: read FD: {} returned {} bytes (buffer path)", fd_,
            result.return_value_);

  return applyRpingToBuffer(buffer, std::move(result));
}

Api::IoCallUint64Result RpingInterceptor::applyRpingToBuffer(Buffer::Instance& buffer,
                                                             Api::IoCallUint64Result result) {
  // If RPING keepalives are still active, check whether the incoming data is a RPING message.
  if (ping_echo_active_ && result.err_ == nullptr && result.return_value_ > 0) {
    const uint64_t expected = ReverseConnectionUtility::PING_MESSAGE.size();

    // Compare up to the expected size using a zero-copy view.
    const uint64_t len = std::min<uint64_t>(buffer.length(), expected);
    const char* data = static_cast<const char*>(buffer.linearize(len));
    absl::string_view peek_sv{data, static_cast<size_t>(len)};

    // Check if we have a complete RPING message.
    if (len == expected && ReverseConnectionUtility::isPingMessage(peek_sv)) {
      // Found a complete RPING. Echo and drain it from the buffer.
      buffer.drain(expected);
      onPingMessage();

      // If buffer only contained RPING, return showing we processed it.
      if (buffer.length() == 0) {
        return Api::IoCallUint64Result{expected, Api::IoError::none()};
      }

      // RPING followed by application data. Disable echo and return the remaining data.
      ENVOY_LOG(trace,
                "RpingInterceptor: received application data after RPING, "
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
                  "RpingInterceptor: partial RPING received ({} bytes), waiting "
                  "for more.",
                  len);
        return result; // Wait for more data.
      }
    }

    // Data is not RPING (complete or partial). Disable echo permanently.
    ENVOY_LOG(trace,
              "RpingInterceptor: received application data ({} bytes), "
              "disabling RPING echo for FD: {}",
              len, fd_);
    ping_echo_active_ = false;
  }

  return result;
}

uint64_t RpingInterceptor::scatterToSlices(absl::string_view src, Buffer::RawSlice* slices,
                                           uint64_t num_slice) {
  uint64_t written = 0;
  for (uint64_t i = 0; i < num_slice && written < src.size(); i++) {
    const uint64_t n = std::min<uint64_t>(slices[i].len_, src.size() - written);
    memcpy(slices[i].mem_, src.data() + written, n);
    written += n;
  }
  ASSERT(written == src.size());
  return written;
}

Api::IoCallUint64Result RpingInterceptor::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                uint64_t num_slice) {
  // Passthrough when there is nothing to strip: inside read()'s dispatch, or once echo has
  // latched off with no partial keepalive staged.
  if (processing_read_ || (!ping_echo_active_ && staged_.empty())) {
    ENVOY_LOG(trace,
              "RpingInterceptor: readv passthrough FD: {} (processing_read={}, ping_echo_active={}, "
              "staged={})",
              fd_, processing_read_, ping_echo_active_, staged_.size());
    return IoSocketHandleImpl::readv(max_length, slices, num_slice);
  }

  const uint64_t expected = ReverseConnectionUtility::PING_MESSAGE.size();

  // staged_ is non-empty only while nothing has been delivered, so the caller is still reading a
  // fresh record header: capacity >= expected then, which the loop below relies on to progress.
  uint64_t capacity = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    capacity += slices[i].len_;
  }
  capacity = std::min<uint64_t>(capacity, max_length);
  if (capacity == 0) {
    return IoSocketHandleImpl::readv(max_length, slices, num_slice);
  }

  ENVOY_LOG(trace,
            "RpingInterceptor: readv enter FD: {} capacity: {} max_length: {} staged: {} bytes",
            fd_, capacity, max_length, staged_.size());

  // Under edge-triggered epoll, keep reading past a consumed RPING until the kernel is drained
  // (short read or EAGAIN); otherwise data coalesced behind it (e.g. a ClientHello) is stranded
  // with no further read event.
  while (true) {
    // Size the read so staged_ + fresh never exceeds caller capacity.
    const uint64_t room = capacity - staged_.size();
    std::vector<char> tmp(room);
    Buffer::RawSlice tmp_slice;
    tmp_slice.mem_ = tmp.data();
    tmp_slice.len_ = room;

    Api::IoCallUint64Result fresh = IoSocketHandleImpl::readv(room, &tmp_slice, 1);

    if (fresh.err_ != nullptr) {
      // Would-block/error: staged_ is preserved for the next call.
      ENVOY_LOG(trace,
                "RpingInterceptor: readv FD: {} underlying read not ok (would_block={}), keeping {} "
                "staged bytes",
                fd_, fresh.wouldBlock(), staged_.size());
      return fresh;
    }
    if (fresh.return_value_ == 0) {
      // EOF. A staged partial keepalive can never complete; drop it and report shutdown.
      ENVOY_LOG(trace, "RpingInterceptor: readv FD: {} EOF, discarding {} staged bytes", fd_,
                staged_.size());
      staged_.clear();
      return Api::IoCallUint64Result{0, Api::IoError::none()};
    }

    std::string assembled;
    assembled.reserve(staged_.size() + fresh.return_value_);
    assembled.append(staged_.data(), staged_.size());
    assembled.append(tmp.data(), static_cast<size_t>(fresh.return_value_));
    ENVOY_LOG(trace, "RpingInterceptor: readv FD: {} read {} fresh bytes, {} assembled with staged",
              fd_, fresh.return_value_, assembled.size());

    const uint64_t len = std::min<uint64_t>(assembled.size(), expected);
    absl::string_view peek_sv{assembled.data(), static_cast<size_t>(len)};

    // Complete RPING at the front: echo it and drop it.
    if (len == expected && ReverseConnectionUtility::isPingMessage(peek_sv)) {
      ENVOY_LOG(trace, "RpingInterceptor: readv FD: {} stripped complete RPING keepalive", fd_);
      onPingMessage();
      staged_.clear();

      absl::string_view remainder{assembled.data() + expected, assembled.size() - expected};
      if (remainder.empty()) {
        // Loop to drain any data coalesced behind the RPING before returning EAGAIN.
        ENVOY_LOG(trace,
                  "RpingInterceptor: readv FD: {} RPING alone, looping to drain any coalesced data",
                  fd_);
        continue;
      }
      ENVOY_LOG(trace,
                "RpingInterceptor: readv FD: {} delivering {} application bytes after RPING, "
                "disabling echo",
                fd_, remainder.size());
      ping_echo_active_ = false;
      const uint64_t written = scatterToSlices(remainder, slices, num_slice);
      return Api::IoCallUint64Result{written, Api::IoError::none()};
    }

    // Proper RPING prefix: the short read means the kernel is drained. Stage it and return EAGAIN;
    // the rest arrives as a fresh readable event.
    if (len < expected) {
      const absl::string_view rping_prefix =
          ReverseConnectionUtility::PING_MESSAGE.substr(0, static_cast<size_t>(len));
      if (peek_sv == rping_prefix) {
        ENVOY_LOG(trace,
                  "RpingInterceptor: readv FD: {} partial RPING prefix ({} bytes), staging and "
                  "returning would-block",
                  fd_, len);
        staged_.assign(assembled.begin(), assembled.end());
        return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
      }
    }

    // Not RPING: echo latches off and all bytes pass through in order.
    ENVOY_LOG(trace,
              "RpingInterceptor: readv FD: {} non-RPING data ({} bytes), disabling echo and "
              "passing through",
              fd_, assembled.size());
    ping_echo_active_ = false;
    staged_.clear();
    const uint64_t written = scatterToSlices(assembled, slices, num_slice);
    return Api::IoCallUint64Result{written, Api::IoError::none()};
  }
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
