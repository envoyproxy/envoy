// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "gtest/gtest.h"
#include "quiche/common/platform/api/quiche_arraysize.h"
#include "quiche/common/platform/api/quiche_endian.h"
#include "quiche/common/platform/api/quiche_string_piece.h"

namespace quiche {

TEST(QuichePlatformTest, Arraysize) {
  int array[] = {0, 1, 2, 3, 4};
  EXPECT_EQ(5, QUICHE_ARRAYSIZE(array));
}

TEST(QuicPlatformTest, StringPiece) {
  std::string s = "bar";
  QuicheStringPiece sp(s);
  EXPECT_EQ('b', sp[0]);
}

TEST(QuichePlatformTest, Endian) {
  EXPECT_EQ(0x1234, QuicheEndian::NetToHost16(QuicheEndian::HostToNet16(0x1234)));
  EXPECT_EQ(0x12345678, QuicheEndian::NetToHost32(QuicheEndian::HostToNet32(0x12345678)));
}

} // namespace quiche
