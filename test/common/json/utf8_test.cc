#include "test/common/json/utf8.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {

TEST(Utf8, OneByte) {
  auto [unicode, consumed] = Utf8::decode("Hello, world!");
  EXPECT_EQ('H', unicode);
  EXPECT_EQ(1, consumed);
}

TEST(Utf8, OneByteInvalid) {
  auto [unicode, consumed] = Utf8::decode("\200");
  EXPECT_EQ(0, consumed);
}

TEST(Utf8, TwoByte) {
  auto [unicode, consumed] = Utf8::decode("α");
  EXPECT_EQ(945, unicode); // https://unicode-table.com/en/03B1/
  EXPECT_EQ(2, consumed);
}

TEST(Utf8, TwoByteInvalid) {
  auto [unicode, consumed] = Utf8::decode("\341\200");
  EXPECT_EQ(0, consumed);
}

TEST(Utf8, ThreeByte) {
  auto [unicode, consumed] = Utf8::decode("コ");
  EXPECT_EQ(0x30B3, unicode); // https://unicode-table.com/en/30B3/
  EXPECT_EQ(3, consumed);
}

TEST(Utf8, ThreeByteInvalid) {
  auto [unicode, consumed] = Utf8::decode("\360\275\200");
  EXPECT_EQ(0, consumed);
}

TEST(Utf8, FourByte) {
  auto [unicode, consumed] = Utf8::decode("\360\235\204\236");
  EXPECT_EQ(0x1d11e, unicode); // https://unicode-table.com/en/1D11E/
  EXPECT_EQ(4, consumed);
}

TEST(Utf8, FourByteInvalid) {
  auto [unicode, consumed] = Utf8::decode("\360\235\204\377");
  EXPECT_EQ(0, consumed);
}

} // namespace Json
} // namespace Envoy
