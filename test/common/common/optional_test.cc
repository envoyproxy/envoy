#include "envoy/common/optional.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(Optional, All) {
  Optional<int> optional;
  EXPECT_FALSE(optional.valid());
  EXPECT_THROW(optional.value(), EnvoyException);

  optional.value(5);
  EXPECT_TRUE(optional.valid());
  EXPECT_EQ(5, optional.value());

  Optional<int> optional_op;
  EXPECT_FALSE(optional_op.valid());
  EXPECT_THROW(optional_op.value(), EnvoyException);

  optional_op = 7;
  EXPECT_TRUE(optional_op.valid());
  EXPECT_EQ(7, optional_op.value());

  const Optional<int> optional_const;
  EXPECT_FALSE(optional_const.valid());
  EXPECT_THROW(optional_const.value(), EnvoyException);

  const Optional<int> optional_const_2(10);
  EXPECT_TRUE(optional_const_2.valid());
  EXPECT_EQ(10, optional_const_2.value());
}
} // namespace Envoy
