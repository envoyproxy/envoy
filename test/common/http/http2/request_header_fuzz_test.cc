// Fuzzer for H2 response HEADERS frames. Unlike codec_impl_fuzz_test, this is
// stateless and focuses only on HEADERS. This technique also plays well with
// uncompressed HEADERS fuzzing.

#include "common/http/exception.h"

#include "test/common/http/http2/frame_replay.h"
#include "test/fuzz/fuzz_runner.h"

using testing::AnyNumber;

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

void Replay(const Frame& frame) {
  ServerCodecFrameInjector codec;
  codec.write(WellKnownFrames::clientConnectionPrefaceFrame());
  codec.write(WellKnownFrames::defaultSettingsFrame());
  codec.write(WellKnownFrames::initialWindowUpdateFrame());
  EXPECT_CALL(codec.server_callbacks_, onGoAway()).Times(AnyNumber());
  EXPECT_CALL(codec.request_decoder_, decodeHeaders_(_, _)).Times(AnyNumber());
  EXPECT_CALL(codec.server_stream_callbacks_, onResetStream(_, _)).Times(AnyNumber());
  try {
    codec.write(frame);
  } catch (const CodecProtocolException& e) {
  }
}

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Frame frame;
  frame.assign(buf, buf + len);
  // Replay with the fuzzer bytes.
  Replay(frame);
  // Try again, but fixup the HEADERS frame to make it a valid HEADERS.
  FrameUtils::fixupHeaders(frame);
  Replay(frame);
}

} // namespace
} // namespace Http2
} // namespace Http
} // namespace Envoy
