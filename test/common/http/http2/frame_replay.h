#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "source/common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Http {
namespace Http2 {

// A byte vector representation of an HTTP/2 frame.
using Frame = std::vector<uint8_t>;

// An HTTP/2 frame derived from a file location.
class FileFrame {
public:
  FileFrame(absl::string_view path);

  Frame& frame() { return frame_; }
  std::unique_ptr<std::istream> istream();

  Frame frame_;
  Api::ApiPtr api_;
};

// Some standards HTTP/2 frames for setting up a connection. The contents for these and the seed
// corpus were captured via logging the hex bytes in codec_impl_test's write() connection mocks in
// setupDefaultConnectionMocks().
class WellKnownFrames {
public:
  static const Frame& clientConnectionPrefaceFrame();
  static const Frame& defaultSettingsFrame();
  static const Frame& initialWindowUpdateFrame();
};

class FrameUtils {
public:
  // Modify a given frame so that it has the HTTP/2 frame header for a valid
  // HEADERS frame.
  static void fixupHeaders(Frame& frame);
};

class CodecFrameInjector {
public:
  CodecFrameInjector(const std::string& injector_name);

  // Writes the data using the Http::Connection's nghttp2 session.
  Http::Status write(const Frame& frame, Http::Connection& connection);

  envoy::config::core::v3::Http2ProtocolOptions options_;
  Stats::IsolatedStoreImpl stats_store_;
  const std::string injector_name_;
};

// Holds mock and environment placeholders for an HTTP/2 client codec. Sets up expectations for
// the behavior of callbacks and the request decoder.
class ClientCodecFrameInjector : public CodecFrameInjector {
public:
  ClientCodecFrameInjector();

  ::testing::NiceMock<Network::MockConnection> client_connection_;
  MockConnectionCallbacks client_callbacks_;
  MockResponseDecoder response_decoder_;
  RequestEncoder* request_encoder_;
  MockStreamCallbacks client_stream_callbacks_;
  Random::MockRandomGenerator random_;
};

// Holds mock and environment placeholders for an HTTP/2 server codec. Sets up expectations for
// the behavior of callbacks and the request decoder.
class ServerCodecFrameInjector : public CodecFrameInjector {
public:
  ServerCodecFrameInjector();

  ::testing::NiceMock<Network::MockConnection> server_connection_;
  MockServerConnectionCallbacks server_callbacks_;
  Random::MockRandomGenerator random_;
  MockRequestDecoder request_decoder_;
  MockStreamCallbacks server_stream_callbacks_;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
