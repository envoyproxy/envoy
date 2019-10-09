#pragma once

#include <chrono>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

/**
 * An abstraction of timespan which can be completed.
 */
class CompletableTimespan {
public:
  virtual ~CompletableTimespan() = default;

  /**
   * Complete the timespan.
   */
  virtual void complete() PURE;
};

/**
 * An individual timespan that flushes its measured value to the histogram on completion.
 * The start time is captured on construction. The timespan must be
 * completed via complete() for it to be stored. If the timespan is deleted this will be treated as
 * a cancellation. The target histogram must represent a quantity of time.
 */
class HistogramCompletableTimespan : public CompletableTimespan {
public:
  HistogramCompletableTimespan(Histogram& histogram, TimeSource& time_source)
      : time_source_(time_source), histogram_(histogram), start_(time_source.monotonicTime()) {}

  /**
   * Get duration in the time unit since the creation of the span.
   */
  template <class TimeUnit> TimeUnit getRawDuration() {
    return std::chrono::duration_cast<TimeUnit>(time_source_.monotonicTime() - start_);
  }

protected:
  TimeSource& time_source_;
  Histogram& histogram_;
  const MonotonicTime start_;
};

using Timespan = HistogramCompletableTimespan;
using TimespanPtr = std::unique_ptr<Timespan>;

} // namespace Stats
} // namespace Envoy
