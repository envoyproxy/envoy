#include "source/common/common/base64.h"
#include "source/common/websocket/handshake.h"

#include "test/mocks/common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace WebSocket {
namespace {

TEST(HandshakeTest, ComputeAcceptMatchesRfc6455Example) {
  EXPECT_EQ(computeAccept("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(HandshakeTest, ComputeAcceptDiffersPerKey) {
  EXPECT_NE(computeAccept("dGhlIHNhbXBsZSBub25jZQ=="), computeAccept("AQIDBAUGBwgJCgsMDQ4PEC=="));
}

TEST(HandshakeTest, GenerateKeyIsBase64Encoded16ByteNonce) {
  testing::NiceMock<Random::MockRandomGenerator> random;
  const std::string key = generateKey(random);

  EXPECT_EQ(key.size(), 24);
  EXPECT_EQ(Base64::decode(key).size(), 16);
}

TEST(HandshakeTest, GenerateKeyVariesWithRandomValues) {
  testing::NiceMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, random())
      .WillOnce(testing::Return(1))
      .WillOnce(testing::Return(2))
      .WillOnce(testing::Return(3))
      .WillOnce(testing::Return(4));

  EXPECT_NE(generateKey(random), generateKey(random));
}

} // namespace
} // namespace WebSocket
} // namespace Envoy
