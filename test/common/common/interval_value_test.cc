#include "source/common/common/interval_value.h"

#include "gtest/gtest.h"

namespace Envoy {

using TestInterval = Interval<int, 5, 10>;

TEST(IntervalTest, Max) { EXPECT_EQ(TestInterval::max_value, 10); }

TEST(IntervalTest, Min) { EXPECT_EQ(TestInterval::min_value, 5); }

using LongIntervalValue = ClosedIntervalValue<long, TestInterval>;

TEST(ClosedIntervalValueTest, Max) { EXPECT_EQ(LongIntervalValue::max().value(), 10); }

TEST(ClosedIntervalValueTest, Min) { EXPECT_EQ(LongIntervalValue::min().value(), 5); }

TEST(ClosedIntervalValueTest, FromBelowMin) { EXPECT_EQ(LongIntervalValue(3).value(), 5); }

TEST(ClosedIntervalValueTest, FromAboveMax) { EXPECT_EQ(LongIntervalValue(20).value(), 10); }

using FloatIntervalValue = ClosedIntervalValue<float, TestInterval>;

TEST(ClosedIntervalValueTest, MixIntAndFloat) {
  EXPECT_EQ(FloatIntervalValue(0).value(), 5.0f);
  EXPECT_EQ(FloatIntervalValue(5).value(), 5.0f);
  EXPECT_EQ(FloatIntervalValue(20).value(), 10.0f);
}

} // namespace Envoy
