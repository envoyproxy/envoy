#pragma once

#include <cstdint>

#include "envoy/common/time.h"

namespace Envoy {

namespace Stats {
class Counter;
}

class FlowControlPauseTracker {
public:
  void onPaused(TimeSource& time_source, Stats::Counter& paused_micros_total);
  void onResumed();
  void onDestruction();

private:
  uint32_t pause_count_{0};
  MonotonicTime pause_start_;
  TimeSource* time_source_{};
  Stats::Counter* paused_micros_total_{};
};

} // namespace Envoy
