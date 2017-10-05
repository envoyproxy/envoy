#pragma once

#include <chrono>

#include "envoy/common/time.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

/**
 * An individual timespan that flushes its measured value to a Duration histogram. The initial time
 * is captured on construction. A timespan must be completed via complete() for it to be stored. If
 * the timespan is deleted this will be treated as a cancellation.
 */
class Timespan {
public:
  Timespan(Histogram& histogram)
      : histogram_(histogram), start_(std::chrono::steady_clock::now()) {}

  virtual ~Timespan() {}

  /**
   * Complete the timespan and send the time to the histogram.
   */
  virtual void complete() { histogram_.recordValue(getRawDuration().count()); }

  /**
   * Get duration since the creation of the span.
   */
  virtual std::chrono::milliseconds getRawDuration() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 start_);
  }

private:
  Histogram& histogram_;
  const MonotonicTime start_;
};

typedef std::unique_ptr<Timespan> TimespanPtr;

} // namespace Stats
} // namespace Envoy
