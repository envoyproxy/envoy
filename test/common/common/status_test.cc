#include "common/common/status.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(Status, Ok) {
  auto status = OkStatus();
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(status.message().empty());
  EXPECT_EQ("OK", ToString(status));
  EXPECT_EQ(StatusCode::Ok, GetStatusCode(status));
  EXPECT_FALSE(IsCodecProtocolError(status));
  EXPECT_FALSE(IsBufferFloodError(status));
  EXPECT_FALSE(IsPrematureResponseError(status));
  EXPECT_FALSE(IsCodecClientError(status));
}

TEST(Status, CodecProtocolError) {
  auto status = CodecProtocolError("foobar");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("foobar", status.message());
  EXPECT_EQ("CodecProtocolError: foobar", ToString(status));
  EXPECT_EQ(StatusCode::CodecProtocolError, GetStatusCode(status));
  EXPECT_TRUE(IsCodecProtocolError(status));
  EXPECT_FALSE(IsBufferFloodError(status));
  EXPECT_FALSE(IsPrematureResponseError(status));
  EXPECT_FALSE(IsCodecClientError(status));
}

TEST(Status, BufferFloodError) {
  auto status = BufferFloodError("foobar");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("foobar", status.message());
  EXPECT_EQ("BufferFloodError: foobar", ToString(status));
  EXPECT_EQ(StatusCode::BufferFloodError, GetStatusCode(status));
  EXPECT_FALSE(IsCodecProtocolError(status));
  EXPECT_TRUE(IsBufferFloodError(status));
  EXPECT_FALSE(IsPrematureResponseError(status));
  EXPECT_FALSE(IsCodecClientError(status));
}

TEST(Status, PrematureResponseError) {
  auto status = PrematureResponseError("foobar", Http::Code::ProxyAuthenticationRequired);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("foobar", status.message());
  EXPECT_EQ("PrematureResponseError: HTTP code: 407: foobar", ToString(status));
  EXPECT_EQ(StatusCode::PrematureResponseError, GetStatusCode(status));
  EXPECT_FALSE(IsCodecProtocolError(status));
  EXPECT_FALSE(IsBufferFloodError(status));
  EXPECT_TRUE(IsPrematureResponseError(status));
  EXPECT_EQ(Http::Code::ProxyAuthenticationRequired, GetPrematureResponseHttpCode(status));
  EXPECT_FALSE(IsCodecClientError(status));
}

TEST(Status, CodecClientError) {
  auto status = CodecClientError("foobar");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("foobar", status.message());
  EXPECT_EQ("CodecClientError: foobar", ToString(status));
  EXPECT_EQ(StatusCode::CodecClientError, GetStatusCode(status));
  EXPECT_FALSE(IsCodecProtocolError(status));
  EXPECT_FALSE(IsBufferFloodError(status));
  EXPECT_FALSE(IsPrematureResponseError(status));
  EXPECT_TRUE(IsCodecClientError(status));
}

} // namespace Envoy
