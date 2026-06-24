#pragma once

#include <cstdint>

#include "envoy/common/time.h"

namespace Envoy {

namespace Stats {
class Counter;
}

class FlowControlPauseTracker {
public:
  void onPaused(TimeSource& time_source);
  void onResumed(TimeSource& time_source, Stats::Counter& paused_micros_total);
  void onDestruction(TimeSource& time_source, Stats::Counter& paused_micros_total);

private:
  uint32_t pause_count_{0};
  MonotonicTime pause_start_;
};

} // namespace Envoy
