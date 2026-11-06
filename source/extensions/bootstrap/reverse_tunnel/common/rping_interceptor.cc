#include "source/extensions/bootstrap/reverse_tunnel/common/rping_interceptor.h"

#include <cstring>
#include <string>

#include "source/common/network/io_socket_error_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

namespace {
// Cap on RPING keepalive echo bytes per read burst while the echo phase is active, shared by the
// read() and readv() paths. Mirrors the per-wake idle path cap in
// UpstreamSocketManager::onPingResponse. A peer that floods keepalives past this budget on a single
// wake closes its own tunnel rather than spinning the worker.
constexpr uint64_t kMaxKeepaliveEchoBytes = 64 * 1024;
} // namespace

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
  // Only inspect successful reads while RPING keepalives are still active.
  if (!ping_echo_active_ || result.err_ != nullptr || result.return_value_ == 0) {
    // While echo is active, a would-block or empty read ends the current read burst. Reset the echo
    // budget so a slow, healthy keepalive stream over the connection lifetime is never torn down;
    // only a burst that floods past the budget on a single wake is closed.
    if (ping_echo_active_) {
      echoed_bytes_ = 0;
    }
    return result;
  }

  // A split echo prefix seeded at checkout sits logically ahead of the freshly read bytes. Prepend
  // it so a completing RPING is stripped rather than delivered to the caller.
  if (!partial_ping_.empty()) {
    buffer.prepend(absl::string_view{partial_ping_.data(), partial_ping_.size()});
    result.return_value_ += partial_ping_.size();
    partial_ping_.clear();
  }

  const uint64_t expected = ReverseConnectionUtility::PING_MESSAGE.size();
  uint64_t stripped = 0;

  // Strip every complete RPING coalesced at the front of the buffer. A stalled initiator can batch
  // several keepalives ahead of the first application bytes, so stop only when the front is no
  // longer a complete `RPING`.
  while (ping_echo_active_) {
    const uint64_t len = std::min<uint64_t>(buffer.length(), expected);
    if (len == 0) {
      // The buffer held only `RPING`s. Report the stripped bytes so the caller does not see EOF.
      return Api::IoCallUint64Result{stripped, Api::IoError::none()};
    }

    // Classify the front of the buffer using a zero-copy view of up to the expected size.
    const char* data = static_cast<const char*>(buffer.linearize(len));
    absl::string_view peek_sv{data, static_cast<size_t>(len)};

    switch (ReverseConnectionUtility::classifyRpingPrefix(peek_sv)) {
    case ReverseConnectionUtility::RpingPrefixMatch::Complete:
      // Found a complete `RPING`. Echo it, drain it, and continue stripping any coalesced behind
      // it.
      buffer.drain(expected);
      onPingMessage();
      stripped += expected;
      echoed_bytes_ += expected;
      if (echoed_bytes_ >= kMaxKeepaliveEchoBytes) {
        // A keepalive flood before any application byte. Close rather than spin the worker, since
        // RawBufferSocket::doRead re-reads until EAGAIN and stripping keeps the buffer drainable.
        ENVOY_LOG(debug, "RpingInterceptor: excessive RPING keepalive flood on FD: {}, closing.",
                  fd_);
        return Api::IoCallUint64Result{0, Network::IoSocketError::create(ECONNRESET)};
      }
      continue;
    case ReverseConnectionUtility::RpingPrefixMatch::PartialPrefix: {
      ENVOY_LOG(trace, "RpingInterceptor: partial RPING received ({} bytes), waiting for more.",
                len);
      // Move the incomplete prefix out of the caller's buffer into partial_ping_, as the readv path
      // does, so it is not delivered to the codec; the next read prepends it to complete the
      // `RPING`. Report a non-zero count so the caller does not see EOF, with the partial now
      // hidden.
      const uint64_t partial_len = buffer.length();
      const char* partial_data = static_cast<const char*>(buffer.linearize(partial_len));
      partial_ping_.assign(partial_data, partial_data + partial_len);
      buffer.drain(partial_len);
      return Api::IoCallUint64Result{stripped + partial_len, Api::IoError::none()};
    }
    case ReverseConnectionUtility::RpingPrefixMatch::NotRping:
      // Application data at the front. Disable echo permanently and pass the remaining data
      // through.
      ENVOY_LOG(trace,
                "RpingInterceptor: received application data ({} bytes), "
                "disabling RPING echo for FD: {}",
                len, fd_);
      ping_echo_active_ = false;
      break;
    }
    break;
  }

  // The remaining bytes in the buffer are application data with the stripped `RPING`s hidden from
  // the caller.
  const uint64_t adjusted =
      (result.return_value_ >= stripped) ? (result.return_value_ - stripped) : 0;
  return Api::IoCallUint64Result{adjusted, Api::IoError::none()};
}

// Copies src across the caller slices. The caller must offer at least src.size() bytes of capacity,
// which holds while echo is active because the TLS BIO classifies with a window of at least a full
// RPING. The pull-rest optimization in readv() only applies when num_slice is one.
uint64_t RpingInterceptor::scatterToSlices(absl::string_view src, Buffer::RawSlice* slices,
                                           uint64_t num_slice) {
  uint64_t written = 0;
  for (uint64_t i = 0; i < num_slice && written < src.size(); i++) {
    const uint64_t n = std::min<uint64_t>(slices[i].len_, src.size() - written);
    memcpy(slices[i].mem_, src.data() + written, n); // NOLINT(safe-memcpy)
    written += n;
  }
  // readv() reads a whole RPING window off the socket before scattering it, so a caller that offers
  // less capacity than src.size() would silently drop already-read bytes and de-synchronize the
  // stream. Fail closed rather than corrupting the stream if a future caller ever violates the
  // contract above.
  RELEASE_ASSERT(written == src.size(),
                 "RpingInterceptor caller offered less slice capacity than a RPING window while "
                 "echo was active");
  return written;
}

Api::IoCallUint64Result RpingInterceptor::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                uint64_t num_slice) {
  // Read straight through without inspecting for `RPING` in two cases:
  //   - processing_read_: the raw read() path is driving this readv() and already strips `RPING`
  //     itself, so doing it here too would double-process.
  //   - ping keepalives have stopped (!ping_echo_active_) and no partial ping is held over from a
  //     previous read (partial_ping_ empty), so there is nothing left to strip.
  if (processing_read_ || (!ping_echo_active_ && partial_ping_.empty())) {
    return IoSocketHandleImpl::readv(max_length, slices, num_slice);
  }

  // Classify one five byte window at a time into a small stack buffer so the caller's slices only
  // ever receive application data. A partial prefix is side buffered in partial_ping_ (at most
  // expected minus one bytes), so no large scratch buffer is allocated per read.
  //
  // epoll wakes us only when new data arrives, so an `RPING` can be packed ahead of real data (for
  // example a TLS ClientHello). Looping until a short read or EAGAIN drains the coalesced bytes.
  constexpr uint64_t kPingSize = ReverseConnectionUtility::PING_MESSAGE.size();
  while (ping_echo_active_) {
    char window[kPingSize];
    const uint64_t held = partial_ping_.size();
    memcpy(window, partial_ping_.data(), held); // NOLINT(safe-memcpy)

    Buffer::RawSlice read_slice;
    read_slice.mem_ = window + held;
    read_slice.len_ = kPingSize - held;
    Api::IoCallUint64Result fresh = IoSocketHandleImpl::readv(read_slice.len_, &read_slice, 1);

    if (fresh.err_ != nullptr) {
      // Would-block or error. partial_ping_ is preserved for the next call, and the read burst
      // ends, so reset the echo budget (see the read() path for the rationale).
      echoed_bytes_ = 0;
      return fresh;
    }
    if (fresh.return_value_ == 0) {
      // EOF. A held partial keepalive can never complete. Drop it and report shutdown.
      echoed_bytes_ = 0;
      partial_ping_.clear();
      return Api::IoCallUint64Result{0, Api::IoError::none()};
    }

    const uint64_t window_len = held + fresh.return_value_;
    absl::string_view window_sv{window, static_cast<size_t>(window_len)};
    switch (ReverseConnectionUtility::classifyRpingPrefix(window_sv)) {
    case ReverseConnectionUtility::RpingPrefixMatch::Complete:
      // A complete RPING. Echo it, drop it, and loop to drain any coalesced behind it.
      onPingMessage();
      partial_ping_.clear();
      echoed_bytes_ += kPingSize;
      if (echoed_bytes_ >= kMaxKeepaliveEchoBytes) {
        // A keepalive flood during the echo phase. Close the connection rather than spin the
        // worker.
        ENVOY_LOG(debug, "RpingInterceptor: excessive RPING keepalive flood on FD: {}, closing.",
                  fd_);
        return Api::IoCallUint64Result{0, Network::IoSocketError::create(ECONNRESET)};
      }
      continue;
    case ReverseConnectionUtility::RpingPrefixMatch::PartialPrefix:
      // Proper but incomplete prefix. The short read means the kernel is drained, so hold it and
      // return EAGAIN; the rest arrives as a fresh readable event. The burst ends, so reset the
      // echo budget.
      echoed_bytes_ = 0;
      partial_ping_.assign(window, window + window_len);
      return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
    case ReverseConnectionUtility::RpingPrefixMatch::NotRping: {
      // Application data begins here. Latch echo off and deliver the window into the caller's
      // slices, then pull any bytes coalesced behind it into the remaining space so the caller
      // receives them together while the bulk stays zero-copy.
      ping_echo_active_ = false;
      partial_ping_.clear();
      const uint64_t written = scatterToSlices(window_sv, slices, num_slice);
      if (num_slice == 1 && max_length > written && slices[0].len_ > written) {
        Buffer::RawSlice rest;
        rest.mem_ = static_cast<char*>(slices[0].mem_) + written;
        rest.len_ = slices[0].len_ - written;
        Api::IoCallUint64Result more = IoSocketHandleImpl::readv(max_length - written, &rest, 1);
        if (more.err_ == nullptr && more.return_value_ > 0) {
          return Api::IoCallUint64Result{written + more.return_value_, Api::IoError::none()};
        }
      }
      return Api::IoCallUint64Result{written, Api::IoError::none()};
    }
    }
    // Unreachable: every classification case above returns or continues.
    PANIC("unexpected RPING classification");
  }

  // Echo latched off during the loop with no held prefix, so read application data zero-copy.
  return IoSocketHandleImpl::readv(max_length, slices, num_slice);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
