#include "source/common/stats/timespan_impl.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"

namespace Envoy {
namespace Stats {

HistogramCompletableTimespanImpl::HistogramCompletableTimespanImpl(Histogram& histogram,
                                                                   TimeSource& time_source)
    : time_source_(time_source), histogram_(histogram), start_(time_source.monotonicTime()) {
  ensureTimeHistogram(histogram);
}

std::chrono::milliseconds HistogramCompletableTimespanImpl::elapsed() const {
  return HistogramCompletableTimespanImpl::elapsedDuration<std::chrono::milliseconds>();
}

void HistogramCompletableTimespanImpl::complete() { histogram_.recordValue(tickCount()); }

void HistogramCompletableTimespanImpl::ensureTimeHistogram(const Histogram& histogram) const {
  switch (histogram.unit()) {
  case Histogram::Unit::Null:
  case Histogram::Unit::Microseconds:
  case Histogram::Unit::Milliseconds:
    return;
  case Histogram::Unit::Unspecified:
  case Histogram::Unit::Bytes:
  case Histogram::Unit::Percent:
    RELEASE_ASSERT(
        false,
        fmt::format("Cannot create a timespan flushing the duration to histogram '{}' because "
                    "it does not measure time. This is a programming error, either pass a "
                    "histogram measuring time or fix the unit of the passed histogram.",
                    histogram.name()));
  }
}

uint64_t HistogramCompletableTimespanImpl::tickCount() const {
  switch (histogram_.unit()) {
  case Histogram::Unit::Null:
    return 0;
  case Histogram::Unit::Microseconds:
    return HistogramCompletableTimespanImpl::elapsedDuration<std::chrono::microseconds>().count();
  case Histogram::Unit::Milliseconds:
    return HistogramCompletableTimespanImpl::elapsedDuration<std::chrono::milliseconds>().count();
  case Histogram::Unit::Unspecified:
  case Histogram::Unit::Bytes:
  case Histogram::Unit::Percent:
    PANIC("not implemented");
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

} // namespace Stats
} // namespace Envoy
