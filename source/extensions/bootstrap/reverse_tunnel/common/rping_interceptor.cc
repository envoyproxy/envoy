#include "source/extensions/bootstrap/reverse_tunnel/common/rping_interceptor.h"

#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

Api::IoCallUint64Result RpingInterceptor::read(Buffer::Instance& buffer,
                                               absl::optional<uint64_t> max_length) {
  // Perform the actual read first.
  Api::IoCallUint64Result result = IoSocketHandleImpl::read(buffer, max_length);
  ENVOY_LOG(trace, "RpingInterceptor: read result: {}", result.return_value_);

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

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
