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

WorkerUtilizationTracker::WorkerUtilizationTracker(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher), last_sample_time_(dispatcher_.timeSource().monotonicTime()) {
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
  pending_poll_ = PendingPoll{prepare_time, timeout};
}

void WorkerUtilizationTracker::onCheck(MonotonicTime check_time) {
  ASSERT(dispatcher_.isThreadSafe());
  if (pending_poll_.has_value()) {
    if (check_time > pending_poll_->prepare_time) {
      MonotonicTime::duration idle_time = check_time - pending_poll_->prepare_time;
      if (pending_poll_->timeout.has_value()) {
        idle_time =
            std::min(idle_time, std::max(MonotonicTime::duration::zero(), *pending_poll_->timeout));
      }
      accumulated_idle_time_ += idle_time;
    }
    pending_poll_.reset();
  }
}

double WorkerUtilizationTracker::sampleUtilization(MonotonicTime now) {
  ASSERT(dispatcher_.isThreadSafe());
  ASSERT(!pending_poll_.has_value());
  if (now <= last_sample_time_) {
    return 0.0;
  }
  const MonotonicTime::duration delta_wall = now - last_sample_time_;
  last_sample_time_ = now;

  const MonotonicTime::duration idle_time =
      std::exchange(accumulated_idle_time_, MonotonicTime::duration::zero());
  const double idle_fraction = std::chrono::duration<double>(idle_time).count() /
                               std::chrono::duration<double>(delta_wall).count();
  return std::clamp(1.0 - idle_fraction, 0.0, 1.0);
}

} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
