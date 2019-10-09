#include "common/stats/timespan_impl.h"

namespace Envoy {
namespace Stats {

HistogramCompletableTimespanImpl::HistogramCompletableTimespanImpl(Histogram& histogram,
                                                                   TimeSource& time_source)
    : time_source_(time_source), histogram_(histogram), start_(time_source.monotonicTime()) {
  if (!histogram.active()) {
    return;
  }

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

uint64_t HistogramCompletableTimespanImpl::elapsedMs() {
  return HistogramCompletableTimespanImpl::elapsed<std::chrono::milliseconds>().count();
}

void HistogramCompletableTimespanImpl::complete() { histogram_.recordValue(tickCount()); }

uint64_t HistogramCompletableTimespanImpl::tickCount() {
  switch (histogram_.unit()) {
  case Histogram::Unit::Unspecified:
  case Histogram::Unit::Bytes:
    // Unreachable, asserted to measure time on construction.
    return 0;
  case Histogram::Unit::Microseconds:
    return HistogramCompletableTimespanImpl::elapsed<std::chrono::microseconds>().count();
  case Histogram::Unit::Milliseconds:
    return HistogramCompletableTimespanImpl::elapsed<std::chrono::milliseconds>().count();
  }

  return 0;
}

} // namespace Stats
} // namespace Envoy
