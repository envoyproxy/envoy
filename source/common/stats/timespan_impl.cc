#include "common/stats/timespan_impl.h"

namespace Envoy {
namespace Stats {

HistogramCompletableTimespanImpl::HistogramCompletableTimespanImpl(Histogram& histogram,
                                                                   TimeSource& time_source)
    : HistogramCompletableTimespan(histogram, time_source) {
  switch (histogram.unit()) {
  case Histogram::Unit::Unspecified:
  case Histogram::Unit::Bytes:
    ASSERT(0);
  case Histogram::Unit::Microseconds:
  case Histogram::Unit::Milliseconds:
    return;
  }

  ASSERT(0);
}

void HistogramCompletableTimespanImpl::complete() { histogram_.recordValue(tickCount()); }

uint64_t HistogramCompletableTimespanImpl::tickCount() {
  switch (histogram_.unit()) {
  case Histogram::Unit::Unspecified:
  case Histogram::Unit::Bytes:
    // Unreachable, asserted to measure time on construction.
    return 0;
  case Histogram::Unit::Microseconds:
    return HistogramCompletableTimespan::getRawDuration<std::chrono::microseconds>().count();
  case Histogram::Unit::Milliseconds:
    return HistogramCompletableTimespan::getRawDuration<std::chrono::milliseconds>().count();
  }

  return 0;
}

} // namespace Stats
} // namespace Envoy
