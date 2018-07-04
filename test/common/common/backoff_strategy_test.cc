#include "common/common/backoff_strategy.h"

#include "test/mocks/runtime/mocks.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

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

TEST(BackOffStrategyTest, ExponentialBackOffReset) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(20, exponential_back_off.nextBackOffMs());

  exponential_back_off.reset();
  EXPECT_EQ(10, exponential_back_off.nextBackOffMs()); // Should start from start
}

TEST(BackOffStrategyTest, ExponentialBackOffResetAfterMaxReached) {
  ExponentialBackOffStrategy exponential_back_off(10, 100, 2);
  EXPECT_EQ(10, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(20, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(40, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(80, exponential_back_off.nextBackOffMs());
  EXPECT_EQ(100, exponential_back_off.nextBackOffMs()); // Should return Max here

  exponential_back_off.reset();

  EXPECT_EQ(10, exponential_back_off.nextBackOffMs()); // Should start from start
}

TEST(BackOffStrategyTest, JitteredBackOffBasicFlow) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredBackOffStrategy jittered_back_off(25, random);
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());
}

TEST(BackOffStrategyTest, JitteredBackOffBasicReset) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredBackOffStrategy jittered_back_off(25, random);
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());

  jittered_back_off.reset();
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs()); // Should start from start
}

TEST(BackOffStrategyTest, JitteredBackOffWithMaxInterval) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(1024));

  JitteredBackOffStrategy jittered_back_off(5, 15, random);
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(9, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(15, jittered_back_off.nextBackOffMs()); // Should return Max here
  EXPECT_EQ(15, jittered_back_off.nextBackOffMs());
}

TEST(BackOffStrategyTest, JitteredBackOffWithMaxIntervalReset) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(1024));

  JitteredBackOffStrategy jittered_back_off(5, 15, random);
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(9, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(15, jittered_back_off.nextBackOffMs()); // Should return Max here

  jittered_back_off.reset();
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs()); // Should start from start
}

} // namespace Envoy
