#include "extensions/filters/http/adaptive_concurrency_limit/common/common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrencyLimit {
namespace Common {

TEST(CommonTest, MinimumMeasurement) {
  MinimumMeasurement<std::chrono::nanoseconds> measurement;
  EXPECT_EQ(std::chrono::nanoseconds(), measurement.get());
  measurement.set(std::chrono::nanoseconds(5));
  EXPECT_EQ(std::chrono::nanoseconds(5), measurement.get());
  measurement.set(std::chrono::nanoseconds(6));
  EXPECT_EQ(std::chrono::nanoseconds(5), measurement.get());
  measurement.set(std::chrono::nanoseconds(4));
  EXPECT_EQ(std::chrono::nanoseconds(4), measurement.get());
  measurement.clear();
  EXPECT_EQ(std::chrono::nanoseconds(), measurement.get());
}

TEST(CommonTest, SampleWindow) {
  SampleWindow sample;
  EXPECT_FALSE(sample.didDrop());
  EXPECT_EQ(0, sample.getSampleCount());
  EXPECT_EQ(0, sample.getMaxInFlightRequests());
  EXPECT_EQ(std::chrono::nanoseconds(INT64_MAX), sample.getMinRtt());
  EXPECT_EQ(std::chrono::nanoseconds(0), sample.getAverageRtt());

  sample.addSample(std::chrono::nanoseconds(10000), 10);
  sample.addSample(std::chrono::nanoseconds(10000), 20);
  sample.addSample(std::chrono::nanoseconds(10000), 15);
  sample.addSample(std::chrono::nanoseconds(10000), 10);

  EXPECT_FALSE(sample.didDrop());
  EXPECT_EQ(4, sample.getSampleCount());
  EXPECT_EQ(20, sample.getMaxInFlightRequests());
  EXPECT_EQ(std::chrono::nanoseconds(10000), sample.getMinRtt());
  EXPECT_EQ(std::chrono::nanoseconds(10000), sample.getAverageRtt());

  sample.addDroppedSample(25);
  sample.addSample(std::chrono::nanoseconds(20000), 10);
  sample.addSample(std::chrono::nanoseconds(20000), 30);
  sample.addSample(std::chrono::nanoseconds(20000), 15);
  sample.addSample(std::chrono::nanoseconds(20000), 10);
  sample.addDroppedSample(30);

  EXPECT_TRUE(sample.didDrop());
  EXPECT_EQ(8, sample.getSampleCount());
  EXPECT_EQ(30, sample.getMaxInFlightRequests());
  EXPECT_EQ(std::chrono::nanoseconds(10000), sample.getMinRtt());
  EXPECT_EQ(std::chrono::nanoseconds(15000), sample.getAverageRtt());
}

} // namespace Common
} // namespace AdaptiveConcurrencyLimit
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy