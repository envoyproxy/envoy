#include "common/stats/timespan_impl.h"

#include "common/common/fmt.h"

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
    RELEASE_ASSERT(
        false,
        fmt::format("Cannot create a timespan flushing the duration to histogram '{}' because "
                    "it does not measure time. This is a programming error, either pass a "
                    "histogram measuring time or fix the unit of the passed histogram.",
                    histogram.name()));
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
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
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Stats
} // namespace Envoy
