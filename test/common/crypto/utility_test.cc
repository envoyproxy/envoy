#include "common/buffer/buffer_impl.h"
#include "common/common/hex.h"
#include "common/crypto/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Common {
namespace Crypto {
namespace {

TEST(UtilityTest, TestSha256Digest) {
  const Buffer::OwnedImpl buffer("test data");
  const auto digest = Utility::getSha256Digest(buffer);
  EXPECT_EQ("916f0027a575074ce72a331777c3478d6513f786a591bd892da1a577bf2335f9",
            Hex::encode(digest));
}

TEST(UtilityTest, TestSha256DigestWithEmptyBuffer) {
  const Buffer::OwnedImpl buffer;
  const auto digest = Utility::getSha256Digest(buffer);
  EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            Hex::encode(digest));
}

TEST(UtilityTest, TestSha256DigestGrowingBuffer) {
  // Adding multiple slices to the buffer
  Buffer::OwnedImpl buffer("slice 1");
  auto digest = Utility::getSha256Digest(buffer);
  EXPECT_EQ("76571770bb46bdf51e1aba95b23c681fda27f6ae56a8a90898a4cb7556e19dcb",
            Hex::encode(digest));
  buffer.add("slice 2");
  digest = Utility::getSha256Digest(buffer);
  EXPECT_EQ("290b462b0fe5edcf6b8532de3ca70da8ab77937212042bb959192ec6c9f95b9a",
            Hex::encode(digest));
  buffer.add("slice 3");
  digest = Utility::getSha256Digest(buffer);
  EXPECT_EQ("29606bbf02fdc40007cdf799de36d931e3587dafc086937efd6599a4ea9397aa",
            Hex::encode(digest));
}

TEST(UtilityTest, TestSha256Hmac) {
  const std::string key = "key";
  auto hmac = Utility::getSha256Hmac(std::vector<uint8_t>(key.begin(), key.end()), "test data");
  EXPECT_EQ("087d9eb992628854842ca4dbf790f8164c80355c1e78b72789d830334927a84c", Hex::encode(hmac));
}

TEST(UtilityTest, TestSha256HmacWithEmptyArguments) {
  auto hmac = Utility::getSha256Hmac(std::vector<uint8_t>(), "");
  EXPECT_EQ("b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad", Hex::encode(hmac));
}

} // namespace
} // namespace Crypto
} // namespace Common
} // namespace Envoy
