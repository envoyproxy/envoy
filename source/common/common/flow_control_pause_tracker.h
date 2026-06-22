#pragma once

#include <cstdint>

#include "envoy/common/time.h"
#include "envoy/stats/stats.h"

namespace Envoy {

class FlowControlPauseTracker {
public:
  void onPaused(TimeSource& time_source);
  void onResumed(TimeSource& time_source, Stats::Counter& paused_micros_total);

private:
  uint32_t pause_count_{0};
  MonotonicTime pause_start_;
};

} // namespace Envoy
