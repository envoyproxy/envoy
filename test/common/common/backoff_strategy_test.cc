#include "common/common/backoff_strategy.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(BackOffStrategyTest, ExponentialBackOffBasicTest) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOff());
  EXPECT_EQ(20, exponential_back_off.nextBackOff());
  EXPECT_EQ(40, exponential_back_off.nextBackOff());
  EXPECT_EQ(80, exponential_back_off.nextBackOff());
}

TEST(BackOffStrategyTest, ExponentialBackOffMaxIntervalReached) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOff());
  EXPECT_EQ(20, exponential_back_off.nextBackOff());
  EXPECT_EQ(40, exponential_back_off.nextBackOff());
  EXPECT_EQ(80, exponential_back_off.nextBackOff());
  EXPECT_EQ(100, exponential_back_off.nextBackOff()); // Should return Max here
  EXPECT_EQ(100, exponential_back_off.nextBackOff()); // Should return Max here
}

TEST(BackOffStrategyTest, ExponentialBackOfReset) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOff());
  EXPECT_EQ(20, exponential_back_off.nextBackOff());

  exponential_back_off.reset();
  EXPECT_EQ(10, exponential_back_off.nextBackOff()); // Should start from start
}

TEST(BackOffStrategyTest, ExponentialBackOfResetAfterMaxReached) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOff());
  EXPECT_EQ(20, exponential_back_off.nextBackOff());
  EXPECT_EQ(40, exponential_back_off.nextBackOff());
  EXPECT_EQ(80, exponential_back_off.nextBackOff());
  EXPECT_EQ(100, exponential_back_off.nextBackOff()); // Should return Max here

  exponential_back_off.reset();

  EXPECT_EQ(10, exponential_back_off.nextBackOff()); // Should start from start
}

} // namespace Envoy
