#include "source/common/common/flow_control_pause_tracker.h"

#include <chrono>

#include "envoy/stats/stats.h"

namespace Envoy {
namespace {

void recordElapsedPause(TimeSource& time_source, MonotonicTime pause_start,
                        Stats::Counter& paused_micros_total) {
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      time_source.monotonicTime() - pause_start);
  if (elapsed.count() > 0) {
    paused_micros_total.add(static_cast<uint64_t>(elapsed.count()));
  }
}

} // namespace

void FlowControlPauseTracker::onPaused(TimeSource& time_source,
                                       Stats::Counter& paused_micros_total) {
  if (pause_count_++ == 0) {
    pause_start_ = time_source.monotonicTime();
    time_source_ = &time_source;
    paused_micros_total_ = &paused_micros_total;
  }
}

void FlowControlPauseTracker::onResumed() {
  if (pause_count_ == 0) {
    return;
  }

  --pause_count_;
  if (pause_count_ != 0) {
    return;
  }

  recordElapsedPause(*time_source_, pause_start_, *paused_micros_total_);
  time_source_ = nullptr;
  paused_micros_total_ = nullptr;
}

void FlowControlPauseTracker::onDestruction() {
  if (pause_count_ == 0) {
    return;
  }

  recordElapsedPause(*time_source_, pause_start_, *paused_micros_total_);
  pause_count_ = 0;
  time_source_ = nullptr;
  paused_micros_total_ = nullptr;
}

} // namespace Envoy
