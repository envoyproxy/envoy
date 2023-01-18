#include <datadog/clock.h>

#include "envoy/common/time.h"

#include "source/extensions/tracers/datadog/time_util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

TEST(DatadogTracerTimeUtilTest, EstimateTime) {
  // Concerns:
  //
  // - If the current system time is after the specified time (likely case),
  //   then the resulting steady time point is set back accordingly.
  // - If the current system time is before the specified time (rare case),
  //   then the resulting steady time point is whatever the clock returns.

  // Modify `now` to change the value returned by `clock`.
  datadog::tracing::TimePoint now;
  datadog::tracing::Clock clock = [&]() { return now; };

  // A little time has elapsed since the SystemTime was measured. The
  // resulting steady time point should be set back by the difference.
  datadog::tracing::TimePoint clock_result = datadog::tracing::default_clock();
  SystemTime argument = clock_result.wall;
  clock_result.wall += std::chrono::microseconds(100);
  now = clock_result;
  datadog::tracing::TimePoint result = estimateTime(argument, clock);
  EXPECT_EQ(result.wall, argument);
  EXPECT_EQ(result.tick, clock_result.tick - std::chrono::microseconds(100));

  // The clock has been set back since the SystemTime was measured. The
  // resulting steady time can't do better than whatever the clock returns
  // (we wouldn't want to set the steady time point into the future).
  clock_result = datadog::tracing::default_clock();
  argument = clock_result.wall;
  clock_result.wall -= std::chrono::milliseconds(100);
  now = clock_result;
  result = estimateTime(argument, clock);
  EXPECT_EQ(result.wall, argument);
  EXPECT_EQ(result.tick, clock_result.tick);
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
