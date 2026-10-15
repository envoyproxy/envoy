#include "source/common/common/base64.h"
#include "source/common/websocket/handshake.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace WebSocket {
namespace {

TEST(HandshakeTest, ComputeAcceptMatchesRfc6455Example) {
  EXPECT_EQ(computeAccept("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(HandshakeTest, GenerateKeyIsBase64Encoded16ByteNonce) {
  const std::string key = generateKey();

  EXPECT_EQ(key.size(), 24);
  EXPECT_EQ(Base64::decode(key).size(), 16);
}

} // namespace
} // namespace WebSocket
} // namespace Envoy
