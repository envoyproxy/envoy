#pragma once

#include <cstdint>
#include <limits>

namespace Envoy {

namespace Http2 {
namespace Utility {

// Limits and defaults for `envoy::config::core::v3::Http2ProtocolOptions` protos.
struct OptionsLimits {
  // disable HPACK compression
  static const uint32_t MIN_HPACK_TABLE_SIZE = 0;
  // initial value from HTTP/2 spec, same as NGHTTP2_DEFAULT_HEADER_TABLE_SIZE from nghttp2
  static const uint32_t DEFAULT_HPACK_TABLE_SIZE = (1 << 12);
  // no maximum from HTTP/2 spec, use unsigned 32-bit maximum
  static const uint32_t MAX_HPACK_TABLE_SIZE = std::numeric_limits<uint32_t>::max();
  // TODO(jwfang): make this 0, the HTTP/2 spec minimum
  static const uint32_t MIN_MAX_CONCURRENT_STREAMS = 1;
  // defaults to maximum, same as nghttp2
  static const uint32_t DEFAULT_MAX_CONCURRENT_STREAMS = (1U << 31) - 1;
  // no maximum from HTTP/2 spec, total streams is unsigned 32-bit maximum,
  // one-side (client/server) is half that, and we need to exclude stream 0.
  // same as NGHTTP2_INITIAL_MAX_CONCURRENT_STREAMS from nghttp2
  static const uint32_t MAX_MAX_CONCURRENT_STREAMS = (1U << 31) - 1;

  // initial value from HTTP/2 spec, same as NGHTTP2_INITIAL_WINDOW_SIZE from nghttp2
  // NOTE: we only support increasing window size now, so this is also the minimum
  // TODO(jwfang): make this 0 to support decrease window size
  static const uint32_t MIN_INITIAL_STREAM_WINDOW_SIZE = (1 << 16) - 1;
  // initial value from HTTP/2 spec is 65535, but we want more (256MiB)
  static const uint32_t DEFAULT_INITIAL_STREAM_WINDOW_SIZE = 256 * 1024 * 1024;
  // maximum from HTTP/2 spec, same as NGHTTP2_MAX_WINDOW_SIZE from nghttp2
  static const uint32_t MAX_INITIAL_STREAM_WINDOW_SIZE = (1U << 31) - 1;

  // CONNECTION_WINDOW_SIZE is similar to STREAM_WINDOW_SIZE, but for connection-level window
  // TODO(jwfang): make this 0 to support decrease window size
  static const uint32_t MIN_INITIAL_CONNECTION_WINDOW_SIZE = (1 << 16) - 1;
  // nghttp2's default connection-level window equals to its stream-level,
  // our default connection-level window also equals to our stream-level
  static const uint32_t DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE = 256 * 1024 * 1024;
  static const uint32_t MAX_INITIAL_CONNECTION_WINDOW_SIZE = (1U << 31) - 1;

  // Default limit on the number of outbound frames of all types.
  static const uint32_t DEFAULT_MAX_OUTBOUND_FRAMES = 10000;
  // Default limit on the number of outbound frames of types PING, SETTINGS and RST_STREAM.
  static const uint32_t DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES = 1000;
  // Default limit on the number of consecutive inbound frames with an empty payload
  // and no end stream flag.
  static const uint32_t DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD = 1;
  // Default limit on the number of inbound frames of type PRIORITY (per stream).
  static const uint32_t DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM = 100;
  // Default limit on the number of inbound frames of type WINDOW_UPDATE (per DATA frame sent).
  static const uint32_t DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT = 10;
};

} // namespace Utility
} // namespace Http2

namespace Http3 {
namespace Utility {

// Limits and defaults for `envoy::config::core::v3::Http3ProtocolOptions` protos.
struct OptionsLimits {
  // The same as kStreamReceiveWindowLimit in QUICHE which is the maximum supported by QUICHE.
  static const uint32_t DEFAULT_INITIAL_STREAM_WINDOW_SIZE = 16 * 1024 * 1024;
  // The same as kSessionReceiveWindowLimit in QUICHE which is the maximum supported by QUICHE.
  static const uint32_t DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE = 24 * 1024 * 1024;
};

} // namespace Utility
} // namespace Http3
} // namespace Envoy
