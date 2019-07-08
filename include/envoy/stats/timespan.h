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
 * An individual timespan that flushes its measured value in time unit (e.g
 * std::chrono::milliseconds). The initial time is captured on construction. A timespan must be
 * completed via complete() for it to be stored. If the timespan is deleted this will be treated as
 * a cancellation.
 */
template <class TimeUnit> class TimespanWithUnit : public CompletableTimespan {
public:
  TimespanWithUnit(Histogram& histogram, TimeSource& time_source)
      : time_source_(time_source), histogram_(histogram), start_(time_source.monotonicTime()) {}

  /**
   * Complete the timespan and send the time to the histogram.
   */
  void complete() override { histogram_.recordValue(getRawDuration().count()); }

  /**
   * Get duration in the time unit since the creation of the span.
   */
  TimeUnit getRawDuration() {
    return std::chrono::duration_cast<TimeUnit>(time_source_.monotonicTime() - start_);
  }

private:
  TimeSource& time_source_;
  Histogram& histogram_;
  const MonotonicTime start_;
};

using Timespan = TimespanWithUnit<std::chrono::milliseconds>;
using TimespanPtr = std::unique_ptr<Timespan>;
using CompletableTimespanPtr = std::unique_ptr<CompletableTimespan>;

} // namespace Stats
} // namespace Envoy
