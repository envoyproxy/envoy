#include "source/extensions/bootstrap/reverse_tunnel/common/rping_interceptor.h"

#include <cstring>
#include <string>

#include "source/common/network/io_socket_error_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

#include "absl/container/fixed_array.h"

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

    // Classify the front of the buffer using a zero-copy view of up to the expected size.
    const uint64_t len = std::min<uint64_t>(buffer.length(), expected);
    const char* data = static_cast<const char*>(buffer.linearize(len));
    absl::string_view peek_sv{data, static_cast<size_t>(len)};

    switch (ReverseConnectionUtility::classifyRpingPrefix(peek_sv)) {
    case ReverseConnectionUtility::RpingPrefixMatch::Complete: {
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
    case ReverseConnectionUtility::RpingPrefixMatch::PartialPrefix:
      ENVOY_LOG(trace, "RpingInterceptor: partial RPING received ({} bytes), waiting for more.",
                len);
      return result; // Wait for more data.
    case ReverseConnectionUtility::RpingPrefixMatch::NotRping:
      // Data is not RPING (complete or partial). Disable echo permanently.
      ENVOY_LOG(trace,
                "RpingInterceptor: received application data ({} bytes), "
                "disabling RPING echo for FD: {}",
                len, fd_);
      ping_echo_active_ = false;
      break;
    }
  }

  return result;
}

uint64_t RpingInterceptor::scatterToSlices(absl::string_view src, Buffer::RawSlice* slices,
                                           uint64_t num_slice) {
  uint64_t written = 0;
  for (uint64_t i = 0; i < num_slice && written < src.size(); i++) {
    const uint64_t n = std::min<uint64_t>(slices[i].len_, src.size() - written);
    memcpy(slices[i].mem_, src.data() + written, n); // NOLINT(safe-memcpy)
    written += n;
  }
  ASSERT(written == src.size());
  return written;
}

Api::IoCallUint64Result RpingInterceptor::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                uint64_t num_slice) {
  // Read straight through without inspecting for RPING in two cases:
  //   - processing_read_: the raw read() path is driving this readv() and already strips RPING
  //     itself, so doing it here too would double-process.
  //   - ping keepalives have stopped (!ping_echo_active_) and no partial ping is held over from a
  //     previous read (partial_ping_ empty), so there is nothing left to strip.
  if (processing_read_ || (!ping_echo_active_ && partial_ping_.empty())) {
    return IoSocketHandleImpl::readv(max_length, slices, num_slice);
  }

  const uint64_t expected = ReverseConnectionUtility::PING_MESSAGE.size();

  // Total space the caller offered; the most we can deliver. A partial ping is only ever held
  // before real data flows, so capacity here is always >= a full ping (5 bytes), keeping
  // room (capacity - partial_ping_) positive so the loop below makes progress.
  uint64_t capacity = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    capacity += slices[i].len_;
  }
  capacity = std::min<uint64_t>(capacity, max_length);
  if (capacity == 0) {
    return IoSocketHandleImpl::readv(max_length, slices, num_slice);
  }

  // epoll wakes us only when new data *arrives*, not while unread data is already sitting in the
  // socket. So a single read can return an RPING with real data (e.g. a TLS ClientHello) packed
  // right behind it. If we swallowed the RPING and returned, that trailing data would sit unread
  // with no further wakeup coming. To avoid that, once we consume an RPING we keep reading until
  // the socket is actually empty (a short read or EAGAIN).
  //
  // Scratch buffer for one read, reused across loop iterations. Sized to full capacity so it fits
  // every iteration; each read uses only `room` of it.
  absl::FixedArray<char> tmp(capacity);
  while (true) {
    // Size the read so partial_ping_ + fresh never exceeds caller capacity.
    const uint64_t room = capacity - partial_ping_.size();
    Buffer::RawSlice tmp_slice;
    tmp_slice.mem_ = tmp.data();
    tmp_slice.len_ = room;

    Api::IoCallUint64Result fresh = IoSocketHandleImpl::readv(room, &tmp_slice, 1);

    if (fresh.err_ != nullptr) {
      // Would-block/error: partial_ping_ is preserved for the next call.
      return fresh;
    }
    if (fresh.return_value_ == 0) {
      // EOF. A held partial keepalive can never complete; drop it and report shutdown.
      partial_ping_.clear();
      return Api::IoCallUint64Result{0, Api::IoError::none()};
    }

    std::string assembled;
    assembled.reserve(partial_ping_.size() + fresh.return_value_);
    assembled.append(partial_ping_.data(), partial_ping_.size());
    assembled.append(tmp.data(), static_cast<size_t>(fresh.return_value_));

    switch (ReverseConnectionUtility::classifyRpingPrefix(assembled)) {
    case ReverseConnectionUtility::RpingPrefixMatch::Complete: {
      // Complete RPING at the front: echo it and drop it.
      onPingMessage();
      partial_ping_.clear();

      absl::string_view remainder{assembled.data() + expected, assembled.size() - expected};
      if (remainder.empty()) {
        // Loop to drain any data coalesced behind the RPING before returning EAGAIN.
        continue;
      }
      ping_echo_active_ = false;
      const uint64_t written = scatterToSlices(remainder, slices, num_slice);
      return Api::IoCallUint64Result{written, Api::IoError::none()};
    }
    case ReverseConnectionUtility::RpingPrefixMatch::PartialPrefix:
      // Proper RPING prefix: the short read means the kernel is drained. Hold it and return
      // EAGAIN; the rest arrives as a fresh readable event.
      partial_ping_.assign(assembled.begin(), assembled.end());
      return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
    case ReverseConnectionUtility::RpingPrefixMatch::NotRping: {
      // Not RPING: echo latches off and all bytes pass through in order.
      ping_echo_active_ = false;
      partial_ping_.clear();
      const uint64_t written = scatterToSlices(assembled, slices, num_slice);
      return Api::IoCallUint64Result{written, Api::IoError::none()};
    }
    }
    // Unreachable: every classification case above returns or continues.
    PANIC("unexpected RPING classification");
  }
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
