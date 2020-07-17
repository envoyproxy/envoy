// Fuzzer for H2 response HEADERS frames. Unlike codec_impl_fuzz_test, this is
// stateless and focuses only on HEADERS. This technique also plays well with
// uncompressed HEADERS fuzzing.

#include "common/http/exception.h"

#include "test/common/http/http2/frame_replay.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

void Replay(const Frame& frame, ServerCodecFrameInjector& codec) {
  // Create the server connection containing the nghttp2 session.
  TestServerConnectionImplNew connection(
      codec.server_connection_, codec.server_callbacks_, codec.stats_store_, codec.options_,
      Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
      envoy::config::core::v3::HttpProtocolOptions::ALLOW);
  Http::Status status = Http::okStatus();
  status = codec.write(WellKnownFrames::clientConnectionPrefaceFrame(), connection);
  status = codec.write(WellKnownFrames::defaultSettingsFrame(), connection);
  status = codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection);
  status = codec.write(frame, connection);
}

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  // Create static objects.
  static ServerCodecFrameInjector codec;
  Frame frame;
  frame.assign(buf, buf + len);
  // Replay with the fuzzer bytes.
  Replay(frame, codec);
  // Try again, but fixup the HEADERS frame to make it a valid HEADERS.
  FrameUtils::fixupHeaders(frame);
  Replay(frame, codec);
}

} // namespace
} // namespace Http2
} // namespace Http
} // namespace Envoy
