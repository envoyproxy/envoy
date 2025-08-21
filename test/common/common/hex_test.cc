#include <string>
#include <vector>

#include "envoy/common/exception.h"

#include "source/common/common/hex.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(Hex, SimpleEncode) {
  std::vector<uint8_t> bytes = {0x01, 0x02, 0x03, 0x0a, 0x0b, 0x0c};
  EXPECT_EQ("0102030a0b0c", Hex::encode(bytes));
}

TEST(Hex, RoundTrip) {
  std::vector<uint8_t> bytes;
  for (uint8_t i = 0; i < UINT8_MAX; i++) {
    bytes.push_back(i);
  }

  std::string hex = Hex::encode(bytes);
  std::vector<uint8_t> decoded = Hex::decode(hex);

  EXPECT_EQ(bytes, decoded);
}

TEST(Hex, BadHex) { EXPECT_EQ(0, Hex::decode("abcde").size()); }

TEST(Hex, DecodeUppercase) { EXPECT_EQ(4, Hex::decode("ABCDEFAB").size()); }

TEST(Hex, UIntToHex) {
  std::string base16_string = Hex::uint64ToHex(2722130815203937912ULL);
  EXPECT_EQ("25c6f38dd0600e78", base16_string);
  EXPECT_EQ("0000000000000000", Hex::uint64ToHex(0ULL));
}

TEST(Hex, UInt32ToHex) {
  EXPECT_EQ("00000000", Hex::uint32ToHex(0));
  EXPECT_EQ("ffffffff", Hex::uint32ToHex(-1));
  EXPECT_EQ("deadbeef", Hex::uint32ToHex(3735928559));
}
} // namespace Envoy
