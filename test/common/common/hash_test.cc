#include "common/common/hash.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(Hash, xxHash) {
  EXPECT_EQ(3728699739546630719U, HashUtil::xxHash64("foo"));
  EXPECT_EQ(5234164152756840025U, HashUtil::xxHash64("bar"));
  EXPECT_EQ(8917841378505826757U, HashUtil::xxHash64("foo\nbar"));
  EXPECT_EQ(4400747396090729504U, HashUtil::xxHash64("lyft"));
  EXPECT_EQ(17241709254077376921U, HashUtil::xxHash64(""));
}

TEST(Hash, djb2CaseInsensitiveHash) {
  EXPECT_EQ(211616621U, HashUtil::djb2CaseInsensitiveHash("foo"));
  EXPECT_EQ(211611524U, HashUtil::djb2CaseInsensitiveHash("bar"));
  EXPECT_EQ(282790909350396U, HashUtil::djb2CaseInsensitiveHash("foo\nbar"));
  EXPECT_EQ(7195212308U, HashUtil::djb2CaseInsensitiveHash("lyft"));
  EXPECT_EQ(5381U, HashUtil::djb2CaseInsensitiveHash(""));
}
} // namespace Envoy
