#pragma once

#include <chrono>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

/**
 * An individual timespan that flushes its measured value (in milliseconds) to a histogram. The
 * initial time is captured on construction. A timespan must be completed via complete() for it to
 * be stored. If the timespan is deleted this will be treated as a cancellation.
 */
class Timespan {
public:
  Timespan(Histogram& histogram, TimeSource& time_source)
      : time_source_(time_source), histogram_(histogram), start_(time_source.monotonicTime()) {}

  /**
   * Complete the timespan and send the time to the histogram.
   */
  void complete() { histogram_.recordValue(getRawDuration().count()); }

  /**
   * Get duration since the creation of the span.
   */
  std::chrono::milliseconds getRawDuration() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time_source_.monotonicTime() -
                                                                 start_);
  }

private:
  TimeSource& time_source_;
  Histogram& histogram_;
  const MonotonicTime start_;
};

typedef std::unique_ptr<Timespan> TimespanPtr;

} // namespace Stats
} // namespace Envoy
