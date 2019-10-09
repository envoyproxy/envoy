#pragma once

#include <chrono>

#include "envoy/common/time.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/timespan.h"

namespace Envoy {
namespace Stats {

/**
 * An individual timespan that flushes its measured value to the histogram on completion.
 * The start time is captured on construction. The timespan must be
 * completed via complete() for it to be stored. If the timespan is deleted this will be treated as
 * a cancellation. The target histogram must represent a quantity of time.
 */
class HistogramCompletableTimespanImpl : public CompletableTimespan {
public:
  HistogramCompletableTimespanImpl(Histogram& histogram, TimeSource& time_source);

  // Stats::CompletableTimespan
  uint64_t elapsedMs() override;
  void complete() override;

private:
  template <typename TimeUnit> TimeUnit elapsed() {
    return std::chrono::duration_cast<TimeUnit>(time_source_.monotonicTime() - start_);
  }
  uint64_t tickCount();

  TimeSource& time_source_;
  Histogram& histogram_;
  const MonotonicTime start_;
};

} // namespace Stats
} // namespace Envoy
