#include "common/common/backoff_strategy.h"

#include "test/mocks/runtime/mocks.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {

TEST(BackOffStrategyTest, JitteredBackOffBasicFlow) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredBackOffStrategy jittered_back_off(25, 30, random);
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());
}

TEST(BackOffStrategyTest, JitteredBackOffBasicReset) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredBackOffStrategy jittered_back_off(25, 30, random);
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

TEST(BackOffStrategyTest, JitteredBackOffLongerDuration) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(1024));

  JitteredBackOffStrategy jittered_back_off(5, 250, random);

  for (size_t i = 0; i < 10000; i++) {
    EXPECT_GE(jittered_back_off.nextBackOffMs(), 1);
  }
}

} // namespace Envoy
