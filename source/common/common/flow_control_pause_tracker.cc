#include "source/common/common/flow_control_pause_tracker.h"

#include <chrono>

namespace Envoy {

void FlowControlPauseTracker::onPaused(TimeSource& time_source) {
  if (pause_count_++ == 0) {
    pause_start_ = time_source.monotonicTime();
  }
}

void FlowControlPauseTracker::onResumed(TimeSource& time_source,
                                        Stats::Counter& paused_micros_total) {
  if (pause_count_ == 0) {
    return;
  }

  --pause_count_;
  if (pause_count_ != 0) {
    return;
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      time_source.monotonicTime() - pause_start_);
  if (elapsed.count() > 0) {
    paused_micros_total.add(static_cast<uint64_t>(elapsed.count()));
  }
}

} // namespace Envoy
