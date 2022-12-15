#include "source/common/singleton/const_singleton.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(ConstSingletonTest, WithFactory) {

  auto const_int = ConstSingleton<int>();

  auto const_int2 = ConstSingleton<int>();

  EXPECT_EQ(const_int.get([]() { return new int(8); }), 8);

  EXPECT_EQ(const_int.get([]() { return new int(9); }), 8);

  EXPECT_EQ(const_int2.get([]() { return new int(19); }), 8);
}

TEST(ConstSingletonTest, WithAndWithoutFactory) {

  auto const_int = ConstSingleton<int>();

  auto const_int2 = ConstSingleton<int>();

  EXPECT_EQ(const_int.get([]() { return new int(-8); }), 8);

  EXPECT_EQ(const_int2.get(), 8);

  EXPECT_EQ(const_int.get(), 8);
}

} // namespace
} // namespace Envoy
