#include "source/server/reset_streams_adapter_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

OverloadActionState createOverloadActionState(double d) {
  return OverloadActionState(UnitFloat(d / 100));
}

TEST(ResetStreamAdapterImplTest, ShouldReturnNothingIfPressureBelowLimit) {
  const double lower_limit = 70;
  const double upper_limit = 95;
  ResetStreamAdapterImpl adapter(lower_limit, upper_limit);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(10)), -1);
}

TEST(ResetStreamAdapterImplTest, ShouldTriggerAtBounds) {
  const double lower_limit = 70;
  const double upper_limit = 95;
  ResetStreamAdapterImpl adapter(lower_limit, upper_limit);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(lower_limit)), 7);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(upper_limit)), 0);
}

TEST(ResetStreamAdapterImplTest, SaturatesWhenAboveUpperLimit) {
  const double lower_limit = 70;
  const double upper_limit = 95;
  ResetStreamAdapterImpl adapter(lower_limit, upper_limit);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(98)), 0);
}

TEST(ResetStreamAdapterImplTest, LinearlyComputesGradation) {
  const double lower_limit = 50;
  const double upper_limit = 90;
  ResetStreamAdapterImpl adapter(lower_limit, upper_limit);

  // Every increment of 5 from lower_limit should result in an increase until we
  // hit the upper_limit.
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(50)), 7);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(55)), 6);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(60)), 5);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(65)), 4);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(70)), 3);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(75)), 2);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(80)), 1);
  EXPECT_EQ(adapter.translateToBucketsToReset(createOverloadActionState(85)), 0);
}

} // namespace
} // namespace Server
} // namespace Envoy
