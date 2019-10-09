#pragma once

#include "envoy/stats/timespan.h"

namespace Envoy {
namespace Stats {

class HistogramCompletableTimespanImpl : public HistogramCompletableTimespan {
public:
  HistogramCompletableTimespanImpl(Histogram& histogram, TimeSource& time_source);

  // Stats::CompletableTimespan
  void complete() override;

private:
  uint64_t tickCount();
};

} // namespace Stats
} // namespace Envoy
