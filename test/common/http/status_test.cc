#include "source/common/http/status.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

TEST(Status, Ok) {
  auto status = okStatus();
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(status.message().empty());
  EXPECT_EQ("OK", toString(status));
  EXPECT_EQ(StatusCode::Ok, getStatusCode(status));
  EXPECT_FALSE(isCodecProtocolError(status));
  EXPECT_FALSE(isBufferFloodError(status));
  EXPECT_FALSE(isPrematureResponseError(status));
  EXPECT_FALSE(isCodecClientError(status));
  EXPECT_FALSE(isInboundFramesWithEmptyPayloadError(status));
  EXPECT_FALSE(isEnvoyOverloadError(status));
}

TEST(Status, CodecProtocolError) {
  auto status = codecProtocolError("foobar");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("foobar", status.message());
  EXPECT_EQ("CodecProtocolError: foobar", toString(status));
  EXPECT_EQ(StatusCode::CodecProtocolError, getStatusCode(status));
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_FALSE(isBufferFloodError(status));
  EXPECT_FALSE(isPrematureResponseError(status));
  EXPECT_FALSE(isCodecClientError(status));
  EXPECT_FALSE(isInboundFramesWithEmptyPayloadError(status));
  EXPECT_FALSE(isEnvoyOverloadError(status));
}

TEST(Status, BufferFloodError) {
  auto status = bufferFloodError("foobar");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("foobar", status.message());
  EXPECT_EQ("BufferFloodError: foobar", toString(status));
  EXPECT_EQ(StatusCode::BufferFloodError, getStatusCode(status));
  EXPECT_FALSE(isCodecProtocolError(status));
  EXPECT_TRUE(isBufferFloodError(status));
  EXPECT_FALSE(isPrematureResponseError(status));
  EXPECT_FALSE(isCodecClientError(status));
  EXPECT_FALSE(isInboundFramesWithEmptyPayloadError(status));
  EXPECT_FALSE(isEnvoyOverloadError(status));
}

TEST(Status, PrematureResponseError) {
  auto status = prematureResponseError("foobar", Http::Code::ProxyAuthenticationRequired);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("foobar", status.message());
  EXPECT_EQ("PrematureResponseError: HTTP code: 407: foobar", toString(status));
  EXPECT_EQ(StatusCode::PrematureResponseError, getStatusCode(status));
  EXPECT_FALSE(isCodecProtocolError(status));
  EXPECT_FALSE(isBufferFloodError(status));
  EXPECT_TRUE(isPrematureResponseError(status));
  EXPECT_EQ(Http::Code::ProxyAuthenticationRequired, getPrematureResponseHttpCode(status));
  EXPECT_FALSE(isCodecClientError(status));
  EXPECT_FALSE(isInboundFramesWithEmptyPayloadError(status));
  EXPECT_FALSE(isEnvoyOverloadError(status));
}

TEST(Status, CodecClientError) {
  auto status = codecClientError("foobar");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("foobar", status.message());
  EXPECT_EQ("CodecClientError: foobar", toString(status));
  EXPECT_EQ(StatusCode::CodecClientError, getStatusCode(status));
  EXPECT_FALSE(isCodecProtocolError(status));
  EXPECT_FALSE(isBufferFloodError(status));
  EXPECT_FALSE(isPrematureResponseError(status));
  EXPECT_TRUE(isCodecClientError(status));
  EXPECT_FALSE(isInboundFramesWithEmptyPayloadError(status));
  EXPECT_FALSE(isEnvoyOverloadError(status));
}

TEST(Status, InboundFramesWithEmptyPayload) {
  auto status = inboundFramesWithEmptyPayloadError();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("Too many consecutive frames with an empty payload", status.message());
  EXPECT_EQ("InboundFramesWithEmptyPayloadError: Too many consecutive frames with an empty payload",
            toString(status));
  EXPECT_EQ(StatusCode::InboundFramesWithEmptyPayload, getStatusCode(status));
  EXPECT_FALSE(isCodecProtocolError(status));
  EXPECT_FALSE(isBufferFloodError(status));
  EXPECT_FALSE(isPrematureResponseError(status));
  EXPECT_FALSE(isCodecClientError(status));
  EXPECT_TRUE(isInboundFramesWithEmptyPayloadError(status));
  EXPECT_FALSE(isEnvoyOverloadError(status));
}

TEST(Status, EnvoyOverloadError) {
  auto status = envoyOverloadError("foobar");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("foobar", status.message());
  EXPECT_EQ("EnvoyOverloadError: foobar", toString(status));
  EXPECT_EQ(StatusCode::EnvoyOverloadError, getStatusCode(status));
  EXPECT_FALSE(isCodecProtocolError(status));
  EXPECT_FALSE(isBufferFloodError(status));
  EXPECT_FALSE(isPrematureResponseError(status));
  EXPECT_FALSE(isCodecClientError(status));
  EXPECT_FALSE(isInboundFramesWithEmptyPayloadError(status));
  EXPECT_TRUE(isEnvoyOverloadError(status));
}

TEST(Status, ReturnIfError) {

  auto outer = [](Status (*inner)()) -> Status {
    RETURN_IF_ERROR(inner());
    return bufferFloodError("boom");
  };

  auto result = outer([]() { return okStatus(); });
  EXPECT_FALSE(result.ok());
  EXPECT_EQ("boom", result.message());
  EXPECT_TRUE(isBufferFloodError(result));
  result = outer([]() { return codecClientError("foobar"); });
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(isCodecClientError(result));
  EXPECT_EQ("foobar", result.message());

  // Check that passing a `Status` object directly into the RETURN_IF_ERROR works.
  auto direct_status = [](const Status& status) -> Status {
    RETURN_IF_ERROR(status);
    return bufferFloodError("baz");
  };
  result = direct_status(codecClientError("foobar"));
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(isCodecClientError(result));
  EXPECT_EQ("foobar", result.message());

  result = direct_status(okStatus());
  EXPECT_FALSE(result.ok());
  EXPECT_EQ("baz", result.message());
  EXPECT_TRUE(isBufferFloodError(result));
}

} // namespace Http
} // namespace Envoy
