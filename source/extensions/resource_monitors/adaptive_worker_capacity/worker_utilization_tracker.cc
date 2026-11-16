#include "source/extensions/resource_monitors/adaptive_worker_capacity/worker_utilization_tracker.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace AdaptiveWorkerCapacity {

WorkerUtilizationTracker::WorkerUtilizationTracker(Event::Dispatcher& dispatcher,
                                                   TimeSource& time_source)
    : dispatcher_(dispatcher), time_source_(time_source),
      last_poll_time_(time_source.monotonicTime()) {
  ASSERT(dispatcher_.isThreadSafe());
  dispatcher_.registerEvwatchObserver(*this);
}

WorkerUtilizationTracker::~WorkerUtilizationTracker() {
  ASSERT(dispatcher_.isThreadSafe());
  dispatcher_.unregisterEvwatchObserver(*this);
}

void WorkerUtilizationTracker::onPrepare(MonotonicTime prepare_time,
                                         std::optional<MonotonicTime::duration> timeout) {
  ASSERT(dispatcher_.isThreadSafe());
  if (busy_start_time_.has_value() && prepare_time > *busy_start_time_) {
    accumulated_busy_time_ += (prepare_time - *busy_start_time_);
  }
  if (timeout.has_value()) {
    busy_start_time_ = prepare_time + *timeout;
  } else {
    busy_start_time_ = std::nullopt;
  }
}

void WorkerUtilizationTracker::onCheck(MonotonicTime check_time) {
  ASSERT(dispatcher_.isThreadSafe());
  if (busy_start_time_.has_value() && check_time > *busy_start_time_) {
    accumulated_busy_time_ += (check_time - *busy_start_time_);
  }
  busy_start_time_ = check_time;
}

double WorkerUtilizationTracker::sampleUtilization() {
  ASSERT(dispatcher_.isThreadSafe());
  const MonotonicTime now = time_source_.monotonicTime();
  if (now <= last_poll_time_) {
    return 0.0;
  }
  if (busy_start_time_.has_value() && now > *busy_start_time_) {
    accumulated_busy_time_ += (now - *busy_start_time_);
    busy_start_time_ = now;
  }
  const MonotonicTime::duration delta_wall = now - last_poll_time_;
  last_poll_time_ = now;

  const MonotonicTime::duration busy_time =
      std::exchange(accumulated_busy_time_, MonotonicTime::duration::zero());
  const double utilization = std::chrono::duration<double>(busy_time).count() /
                             std::chrono::duration<double>(delta_wall).count();
  return std::clamp(utilization, 0.0, 1.0);
}

} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
