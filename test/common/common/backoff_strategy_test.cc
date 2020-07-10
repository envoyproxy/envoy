#include "common/common/backoff_strategy.h"

#include "test/mocks/common.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {

TEST(BackOffStrategyTest, JitteredBackOffBasicFlow) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredBackOffStrategy jittered_back_off(25, 30, random);
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());
}

TEST(BackOffStrategyTest, JitteredBackOffBasicReset) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredBackOffStrategy jittered_back_off(25, 30, random);
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());

  jittered_back_off.reset();
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs()); // Should start from start
}

TEST(BackOffStrategyTest, JitteredBackOffDoesntOverflow) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(std::numeric_limits<uint64_t>::max() - 1));

  JitteredBackOffStrategy jittered_back_off(1, std::numeric_limits<uint64_t>::max(), random);
  for (int iter = 0; iter < 100; ++iter) {
    EXPECT_GE(std::numeric_limits<uint64_t>::max(), jittered_back_off.nextBackOffMs());
  }
  EXPECT_EQ(std::numeric_limits<uint64_t>::max() - 1, jittered_back_off.nextBackOffMs());
}

TEST(BackOffStrategyTest, JitteredBackOffWithMaxInterval) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(9999));

  JitteredBackOffStrategy jittered_back_off(5, 100, random);
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(9, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(19, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(39, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(79, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(99, jittered_back_off.nextBackOffMs()); // Should return Max here
  EXPECT_EQ(99, jittered_back_off.nextBackOffMs());
}

TEST(BackOffStrategyTest, JitteredBackOffWithMaxIntervalReset) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(9999));

  JitteredBackOffStrategy jittered_back_off(5, 100, random);
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(9, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(19, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(39, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(79, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(99, jittered_back_off.nextBackOffMs()); // Should return Max here
  EXPECT_EQ(99, jittered_back_off.nextBackOffMs());

  jittered_back_off.reset();
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(9, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(19, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(39, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(79, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(99, jittered_back_off.nextBackOffMs()); // Should return Max here
  EXPECT_EQ(99, jittered_back_off.nextBackOffMs());
}

TEST(BackOffStrategyTest, FixedBackOffBasicReset) {
  FixedBackOffStrategy fixed_back_off(30);
  EXPECT_EQ(30, fixed_back_off.nextBackOffMs());
  EXPECT_EQ(30, fixed_back_off.nextBackOffMs());

  fixed_back_off.reset();
  EXPECT_EQ(30, fixed_back_off.nextBackOffMs());
}

} // namespace Envoy
