#include "common/common/backoff_strategy.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(BackOffStrategyTest, ExponentialBackOffBasicTest) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(20, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(40, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(80, exponential_back_off.nextBackOffMs());
}

TEST(BackOffStrategyTest, ExponentialBackOffFractionalMultiplier) {
  ExponentialBackOffStrategy exponential_back_off(10, 50, 1.5);
  EXPECT_EQ(10, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(15, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(23, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(35, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(50, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(50, exponential_back_off.nextBackOffMs());
}

TEST(BackOffStrategyTest, ExponentialBackOffMaxIntervalReached) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(20, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(40, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(80, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(100, exponential_back_off.nextBackOffMs()); // Should return Max here
  EXPECT_EQ(100, exponential_back_off.nextBackOffMs()); // Should return Max here
}

TEST(BackOffStrategyTest, ExponentialBackOfReset) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(20, exponential_back_off.nextBackOffMs());

  exponential_back_off.reset();
  EXPECT_EQ(10, exponential_back_off.nextBackOffMs()); // Should start from start
}

TEST(BackOffStrategyTest, ExponentialBackOfResetAfterMaxReached) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(20, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(40, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(80, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(100, exponential_back_off.nextBackOffMs()); // Should return Max here

  exponential_back_off.reset();

  EXPECT_EQ(10, exponential_back_off.nextBackOffMs()); // Should start from start
}

} // namespace Envoy
