#include "test/common/http/http2/frame_replay.h"

#include "source/common/common/hex.h"
#include "source/common/common/macros.h"
#include "source/common/http/utility.h"

#include "test/common/http/common.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Http {
namespace Http2 {

FileFrame::FileFrame(absl::string_view path) : api_(Api::createApiForTest()) {
  const std::string contents = api_->fileSystem()
                                   .fileReadToEnd(TestEnvironment::runfilesPath(
                                       "test/common/http/http2/" + std::string(path)))
                                   .value();
  frame_.resize(contents.size());
  contents.copy(reinterpret_cast<char*>(frame_.data()), frame_.size());
}

std::unique_ptr<std::istream> FileFrame::istream() {
  const std::string frame_string{reinterpret_cast<char*>(frame_.data()), frame_.size()};
  return std::make_unique<std::istringstream>(frame_string);
}

const Frame& WellKnownFrames::clientConnectionPrefaceFrame() {
  CONSTRUCT_ON_FIRST_USE(std::vector<uint8_t>,
                         {0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x32,
                          0x2e, 0x30, 0x0d, 0x0a, 0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a});
}

const Frame& WellKnownFrames::defaultSettingsFrame() {
  CONSTRUCT_ON_FIRST_USE(std::vector<uint8_t>,
                         {0x00, 0x00, 0x0c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
                          0x7f, 0xff, 0xff, 0xff, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00});
}

const Frame& WellKnownFrames::initialWindowUpdateFrame() {
  CONSTRUCT_ON_FIRST_USE(std::vector<uint8_t>, {0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
                                                0x00, 0x0f, 0xff, 0x00, 0x01});
}

void FrameUtils::fixupHeaders(Frame& frame) {
  constexpr size_t frame_header_len = 9; // from RFC 7540
  while (frame.size() < frame_header_len) {
    frame.emplace_back(0x00);
  }
  size_t headers_len = frame.size() - frame_header_len;
  frame[2] = headers_len & 0xff;
  headers_len >>= 8;
  frame[1] = headers_len & 0xff;
  headers_len >>= 8;
  frame[0] = headers_len & 0xff;
  // HEADERS frame with END_STREAM | END_HEADERS for stream 1.
  size_t offset = 3;
  for (const uint8_t b : {0x01, 0x05, 0x00, 0x00, 0x00, 0x01}) {
    frame[offset++] = b;
  }
}

CodecFrameInjector::CodecFrameInjector(const std::string& injector_name)
    : options_(::Envoy::Http2::Utility::initializeAndValidateOptions(
                   envoy::config::core::v3::Http2ProtocolOptions())
                   .value()),
      injector_name_(injector_name) {}

ClientCodecFrameInjector::ClientCodecFrameInjector() : CodecFrameInjector("server") {
  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
        ENVOY_LOG_MISC(
            trace, "client write: {}",
            Hex::encode(static_cast<uint8_t*>(data.linearize(data.length())), data.length()));
        data.drain(data.length());
      }));
}

ServerCodecFrameInjector::ServerCodecFrameInjector() : CodecFrameInjector("client") {
  EXPECT_CALL(server_callbacks_, newStream(_, _))
      .WillRepeatedly(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        encoder.getStream().addCallbacks(server_stream_callbacks_);
        return request_decoder_;
      }));

  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
        ENVOY_LOG_MISC(
            trace, "server write: {}",
            Hex::encode(static_cast<uint8_t*>(data.linearize(data.length())), data.length()));
        data.drain(data.length());
      }));
}

Http::Status CodecFrameInjector::write(const Frame& frame, Http::Connection& connection) {
  Buffer::OwnedImpl buffer;
  buffer.add(frame.data(), frame.size());
  ENVOY_LOG_MISC(trace, "{} write: {}", injector_name_, Hex::encode(frame.data(), frame.size()));
  auto status = Http::okStatus();
  while (buffer.length() > 0 && status.ok()) {
    status = connection.dispatch(buffer);
  }
  ENVOY_LOG_MISC(trace, "Status: {}", status.message());
  return status;
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
