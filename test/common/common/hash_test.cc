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
} // namespace Envoy
