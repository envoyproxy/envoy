// Fuzzer for H2 response HEADERS frames. Unlike codec_impl_fuzz_test, this is
// stateless and focuses only on HEADERS. This technique also plays well with
// uncompressed HEADERS fuzzing.

#include "source/common/http/exception.h"

#include "test/common/http/http2/codec_impl_test_util.h"
#include "test/common/http/http2/frame_replay.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

void replay(const Frame& frame, ServerCodecFrameInjector& codec) {
  // Create the server connection containing the nghttp2 session.
  TestServerConnectionImpl connection(
      codec.server_connection_, codec.server_callbacks_, *codec.stats_store_.rootScope(),
      codec.options_, codec.random_, Http::DEFAULT_MAX_REQUEST_HEADERS_KB,
      Http::DEFAULT_MAX_HEADERS_COUNT, envoy::config::core::v3::HttpProtocolOptions::ALLOW);
  Http::Status status = Http::okStatus();
  status = codec.write(WellKnownFrames::clientConnectionPrefaceFrame(), connection);
  status = codec.write(WellKnownFrames::defaultSettingsFrame(), connection);
  status = codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection);
  status = codec.write(frame, connection);
}

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
#ifdef ENVOY_ENABLE_UHV
  // Temporarily disable oghttp2 for these fuzz tests.
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.http2_use_oghttp2", "false"}});
#endif

  // Create static objects.
  static ServerCodecFrameInjector codec;
  Frame frame;
  frame.assign(buf, buf + len);
  // Replay with the fuzzer bytes.
  replay(frame, codec);
  // Try again, but fixup the HEADERS frame to make it a valid HEADERS.
  FrameUtils::fixupHeaders(frame);
  replay(frame, codec);
}

} // namespace
} // namespace Http2
} // namespace Http
} // namespace Envoy
