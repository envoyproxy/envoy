#pragma once

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "test/common/http/http2/http2_frame.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/integration/h2_capture_fuzz.pb.h"
#include "test/integration/http_integration.h"

namespace Envoy {

class H2FuzzIntegrationTest : public HttpIntegrationTest {
public:
  H2FuzzIntegrationTest(Network::Address::IpVersion version)
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, version) {}

  void initialize() override;
  void replay(const test::integration::H2CaptureFuzzTestCase&);

private:
  // Maps between the enum value in h2_capture_fuzz.proto to the correct
  // function in test/common/http/http2/http2_frame.h
  // TODO(adip): some methods receive the stream index, which is currently set to 1, this should
  // be fuzzed further
  static constexpr uint32_t stream_index_ = 1;
  static constexpr const char* host_ = "host";
  static constexpr const char* path_ = "/test/long/url";
  Envoy::Http::Http2::Http2Frame (*const frameMakers[17])(){
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makePingFrame();
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptySettingsFrame();
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptySettingsFrame(
            Envoy::Http::Http2::Http2Frame::SettingsFlags::Ack);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptyHeadersFrame(stream_index_);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptyHeadersFrame(
            stream_index_, Envoy::Http::Http2::Http2Frame::HeadersFlags::EndStream);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptyHeadersFrame(
            stream_index_, Envoy::Http::Http2::Http2Frame::HeadersFlags::EndHeaders);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptyContinuationFrame(stream_index_);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptyContinuationFrame(
            stream_index_, Envoy::Http::Http2::Http2Frame::HeadersFlags::EndStream);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptyContinuationFrame(
            stream_index_, Envoy::Http::Http2::Http2Frame::HeadersFlags::EndHeaders);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptyDataFrame(stream_index_);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeEmptyDataFrame(
            stream_index_, Envoy::Http::Http2::Http2Frame::DataFlags::EndStream);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makePriorityFrame(stream_index_, 1);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeWindowUpdateFrame(stream_index_, 60);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeMalformedRequest(stream_index_);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeMalformedRequestWithZerolenHeader(stream_index_,
                                                                                     host_, path_);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makeRequest(stream_index_, host_, path_);
      },
      []() -> Envoy::Http::Http2::Http2Frame {
        return Envoy::Http::Http2::Http2Frame::makePostRequest(stream_index_, host_, path_);
      }};
};
} // namespace Envoy
