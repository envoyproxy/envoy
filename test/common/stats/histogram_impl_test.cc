#include "test/mocks/stats/mocks.h"

#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Stats {
namespace {

TEST(HistogramImplTest, Basic) {
  MockStore store;
  ParentHistogramImpl histogram("test", store, "test", std::vector<Tag>());
  const HistogramStatistics &intervalStats = histogram.intervalStatistics(),
                            &cumulativeStats = histogram.cumulativeStatistics();

  // A histogram should retain the name it was given.
  ASSERT_EQ("test", histogram.name());
  EXPECT_STREQ("test", histogram.nameCStr());

  // A histogram that hasn't been written to is not "used" and has no interesting statistics.
  ASSERT_FALSE(histogram.used());
  ASSERT_EQ(0, intervalStats.sampleCount());
  ASSERT_EQ(0, cumulativeStats.sampleCount());

  // Recording a value marks the histogram as "used" and delivers to sinks.
  EXPECT_CALL(store, deliverHistogramToSinks(_, 123));
  histogram.recordValue(123);
  ASSERT_TRUE(histogram.used());

  // Merging should populate statistics for the last interval, and cumulative statistics. Note that
  // the sample sum loses precision; supposedly 125 is close enough to 123...
  histogram.merge();
  ASSERT_EQ(1, intervalStats.sampleCount());
  ASSERT_EQ(125, intervalStats.sampleSum());
  ASSERT_EQ(1, cumulativeStats.sampleCount());
  ASSERT_EQ(125, cumulativeStats.sampleSum());

  // Recording another value and merging again should yield fresh interval statistics, and add to
  // cumulative statistics.
  EXPECT_CALL(store, deliverHistogramToSinks(_, 456));
  histogram.recordValue(456);
  histogram.merge();
  ASSERT_EQ(1, intervalStats.sampleCount());
  ASSERT_EQ(455, intervalStats.sampleSum()); // 455 ~= 456
  ASSERT_EQ(2, cumulativeStats.sampleCount());
  ASSERT_EQ(580, cumulativeStats.sampleSum()); // 580 == 125 + 455.

  // String quantile and bucket summaries should look sane.
  EXPECT_EQ("P0(450,120) P25(452.5,125) P50(455,130) P75(457.5,455) P90(459,458) P95(459.5,459) "
            "P99(459.9,459.8) P99.5(459.95,459.9) P99.9(459.99,459.98) P100(460,460)",
            histogram.quantileSummary());
  EXPECT_EQ("B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(0,0) B50(0,0) B100(0,0) B250(0,1) B500(1,2) "
            "B1000(1,2) B2500(1,2) B5000(1,2) B10000(1,2) B30000(1,2) B60000(1,2) B300000(1,2) "
            "B600000(1,2) B1.8e+06(1,2) B3.6e+06(1,2)",
            histogram.bucketSummary());
}

} // namespace
} // namespace Stats
} // namespace Envoy