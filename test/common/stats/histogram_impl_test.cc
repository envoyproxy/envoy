#include "test/mocks/stats/mocks.h"

#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Stats {
namespace {

struct HistogramImplTest : public testing::Test {
  HistogramImplTest()
      : histogram_("test", store_, "test", std::vector<Tag>()),
        interval_stats_(histogram_.intervalStatistics()),
        cumulative_stats_(histogram_.cumulativeStatistics()) {}

  NiceMock<MockStore> store_;
  ParentHistogramImpl histogram_;
  const HistogramStatistics &interval_stats_, &cumulative_stats_;
};

// A histogram should retain the name it was given.
TEST_F(HistogramImplTest, Name) {
  EXPECT_EQ("test", histogram_.name());
  EXPECT_STREQ("test", histogram_.nameCStr());
}

// A histogram that hasn't been written to is not "used" and has no interesting statistics.
TEST_F(HistogramImplTest, Empty) {
  EXPECT_FALSE(histogram_.used());
  EXPECT_EQ(0, interval_stats_.sampleCount());
  EXPECT_EQ(0, cumulative_stats_.sampleCount());
}

// Recording a value marks the histogram as "used" and delivers to sinks.
TEST_F(HistogramImplTest, RecordValue) {
  EXPECT_CALL(store_, deliverHistogramToSinks(_, 123));
  histogram_.recordValue(123);
  EXPECT_TRUE(histogram_.used());
}

// Merging should populate statistics for the last interval, and cumulative statistics. Note that
// the sample sum loses precision.
TEST_F(HistogramImplTest, RecordAndMerge) {
  histogram_.recordValue(123);
  histogram_.merge();
  EXPECT_EQ(1, interval_stats_.sampleCount());
  EXPECT_EQ(125, interval_stats_.sampleSum()); // 125 ~= 123
  EXPECT_EQ(1, cumulative_stats_.sampleCount());
  EXPECT_EQ(125, cumulative_stats_.sampleSum()); // same
}

// Recording another value and merging again should yield fresh interval statistics, and add to
// cumulative statistics. Again, the sample sum loses precision.
TEST_F(HistogramImplTest, RecordAndMergeTwice) {
  histogram_.recordValue(123);
  histogram_.merge();
  histogram_.recordValue(456);
  histogram_.merge();
  EXPECT_EQ(1, interval_stats_.sampleCount());
  EXPECT_EQ(455, interval_stats_.sampleSum()); // 455 ~= 456
  EXPECT_EQ(2, cumulative_stats_.sampleCount());
  EXPECT_EQ(580, cumulative_stats_.sampleSum()); // 580 == 125 + 455.
}

} // namespace
} // namespace Stats
} // namespace Envoy
