#include "source/common/common/backoff_strategy.h"

#include "test/mocks/common.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {

TEST(ExponentialBackOffStrategyTest, JitteredBackOffBasicFlow) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredExponentialBackOffStrategy jittered_back_off(25, 30, random);
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());
}

TEST(ExponentialBackOffStrategyTest, JitteredBackOffBasicReset) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredExponentialBackOffStrategy jittered_back_off(25, 30, random);
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs()); // 25 % 27
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());

  jittered_back_off.reset();
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs()); // Should start from start
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());

  jittered_back_off.reset(26);
  EXPECT_EQ(1, jittered_back_off.nextBackOffMs()); // 26 % 27
}

TEST(ExponentialBackOffStrategyTest, JitteredBackOffDoesntOverflow) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(std::numeric_limits<uint64_t>::max() - 1));

  JitteredExponentialBackOffStrategy jittered_back_off(1, std::numeric_limits<uint64_t>::max(),
                                                       random);
  for (int iter = 0; iter < 100; ++iter) {
    EXPECT_GE(std::numeric_limits<uint64_t>::max(), jittered_back_off.nextBackOffMs());
  }
  EXPECT_EQ(std::numeric_limits<uint64_t>::max() - 1, jittered_back_off.nextBackOffMs());
}

TEST(ExponentialBackOffStrategyTest, JitteredBackOffWithMaxInterval) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(9999));

  JitteredExponentialBackOffStrategy jittered_back_off(5, 100, random);
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(9, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(19, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(39, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(79, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(99, jittered_back_off.nextBackOffMs()); // Should return Max here
  EXPECT_EQ(99, jittered_back_off.nextBackOffMs());
}

TEST(ExponentialBackOffStrategyTest, JitteredBackOffWithMaxIntervalReset) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(9999));

  JitteredExponentialBackOffStrategy jittered_back_off(5, 100, random);
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

  jittered_back_off.reset(4);
  EXPECT_EQ(3, jittered_back_off.nextBackOffMs());
}

TEST(LowerBoundBackOffStrategyTest, JitteredBackOffWithLowRandomValue) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(22));

  JitteredLowerBoundBackOffStrategy jittered_lower_bound_back_off(500, random);
  EXPECT_EQ(522, jittered_lower_bound_back_off.nextBackOffMs());
}

TEST(LowerBoundBackOffStrategyTest, JitteredBackOffWithHighRandomValue) {
  NiceMock<Random::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(9999));

  JitteredLowerBoundBackOffStrategy jittered_lower_bound_back_off(500, random);
  EXPECT_EQ(749, jittered_lower_bound_back_off.nextBackOffMs());
}

TEST(FixedBackOffStrategyTest, FixedBackOffBasicReset) {
  FixedBackOffStrategy fixed_back_off(30);
  EXPECT_EQ(30, fixed_back_off.nextBackOffMs());
  EXPECT_EQ(30, fixed_back_off.nextBackOffMs());

  fixed_back_off.reset();
  EXPECT_EQ(30, fixed_back_off.nextBackOffMs());

  fixed_back_off.reset(20);
  EXPECT_EQ(20, fixed_back_off.nextBackOffMs());
}

TEST(BackOffStrategyUtilsTest, InvalidConfig) {
  {
    // Valid config.
    envoy::config::core::v3::BackoffStrategy backoff_strategy;
    backoff_strategy.mutable_base_interval()->set_seconds(2);
    backoff_strategy.mutable_max_interval()->set_seconds(3);
    EXPECT_TRUE(BackOffStrategyUtils::validateBackOffStrategyConfig(backoff_strategy, 1, 10).ok());
  }

  {
    // Max interval is lower than base interval.
    envoy::config::core::v3::BackoffStrategy backoff_strategy;
    backoff_strategy.mutable_base_interval()->set_seconds(3);
    backoff_strategy.mutable_max_interval()->set_seconds(2);
    EXPECT_TRUE(!BackOffStrategyUtils::validateBackOffStrategyConfig(backoff_strategy, 1, 10).ok());
  }

  {
    // Max interval is lower than base interval.
    envoy::config::core::v3::BackoffStrategy backoff_strategy;
    backoff_strategy.mutable_max_interval()->set_nanos(2000000);
    EXPECT_TRUE(!BackOffStrategyUtils::validateBackOffStrategyConfig(backoff_strategy, 3, 10).ok());
  }
}

} // namespace Envoy
