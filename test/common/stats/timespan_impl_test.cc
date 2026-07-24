#include <chrono>

#include "source/common/stats/timespan_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

using testing::NiceMock;

class TimespanImplTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_system_;
};

TEST_F(TimespanImplTest, ElapsedAndCompleteMilliseconds) {
  NiceMock<MockHistogram> hist;
  hist.unit_ = Histogram::Unit::Milliseconds;
  HistogramCompletableTimespanImpl span(hist, time_system_);
  time_system_.advanceTimeWait(std::chrono::milliseconds(7));
  EXPECT_EQ(std::chrono::milliseconds(7), span.elapsed());
  EXPECT_CALL(hist, recordValue(7));
  span.complete();
}

TEST_F(TimespanImplTest, ElapsedAndCompleteMicroseconds) {
  NiceMock<MockHistogram> hist;
  hist.unit_ = Histogram::Unit::Microseconds;
  HistogramCompletableTimespanImpl span(hist, time_system_);
  time_system_.advanceTimeWait(std::chrono::microseconds(150));
  // elapsed() always returns milliseconds; 150us rounds down to 0ms.
  EXPECT_EQ(std::chrono::milliseconds(0), span.elapsed());
  EXPECT_CALL(hist, recordValue(150));
  span.complete();
}

// Exercises the Histogram::Unit::Null branches in ensureTimeHistogram and tickCount.
TEST_F(TimespanImplTest, NullUnit) {
  NiceMock<MockHistogram> hist;
  hist.unit_ = Histogram::Unit::Null;
  HistogramCompletableTimespanImpl span(hist, time_system_);
  time_system_.advanceTimeWait(std::chrono::milliseconds(5));
  EXPECT_CALL(hist, recordValue(0));
  span.complete();
}

// Constructing a Timespan with a non-time histogram unit fails a release assert.
TEST_F(TimespanImplTest, UnspecifiedHistogramDies) {
  NiceMock<MockHistogram> hist;
  hist.unit_ = Histogram::Unit::Unspecified;
  EXPECT_DEATH(HistogramCompletableTimespanImpl(hist, time_system_), "does not measure time");
}

TEST_F(TimespanImplTest, BytesHistogramDies) {
  NiceMock<MockHistogram> hist;
  hist.unit_ = Histogram::Unit::Bytes;
  EXPECT_DEATH(HistogramCompletableTimespanImpl(hist, time_system_), "does not measure time");
}

TEST_F(TimespanImplTest, PercentHistogramDies) {
  NiceMock<MockHistogram> hist;
  hist.unit_ = Histogram::Unit::Percent;
  EXPECT_DEATH(HistogramCompletableTimespanImpl(hist, time_system_), "does not measure time");
}

} // namespace
} // namespace Stats
} // namespace Envoy
