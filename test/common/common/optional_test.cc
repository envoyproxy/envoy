#include "envoy/common/optional.h"

#include "gtest/gtest.h"

namespace Lyft {
TEST(Optional, All) {
  Optional<int> optional;
  EXPECT_FALSE(optional.valid());
  EXPECT_THROW(optional.value(), EnvoyException);

  optional.value(5);
  EXPECT_TRUE(optional.valid());
  EXPECT_EQ(5, optional.value());
}
} // Lyft