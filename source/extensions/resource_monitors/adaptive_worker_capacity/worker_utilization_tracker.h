#pragma once

#include <optional>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/evwatch.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace AdaptiveWorkerCapacity {

// WorkerUtilizationTrackerInterface defines the interface for measuring a worker thread's
// event-loop utilization. All methods must be called on the worker's dispatcher thread.
class WorkerUtilizationTrackerInterface {
public:
  virtual ~WorkerUtilizationTrackerInterface() = default;

  // Calculates thread utilization in [0.0, 1.0] over the window ending at now (a monotonic
  // timestamp obtained from the worker dispatcher's TimeSource) since the previous sample.
  virtual double sampleUtilization(MonotonicTime now) = 0;
};

// WorkerUtilizationTracker measures event-loop utilization on a persistent blocking worker event
// loop (RunType::Block or RunType::RunUntilExit).
//
// Because sampleUtilization(now) is always invoked from a callback on dispatcher's thread (between
// onCheck and onPrepare, never while blocked inside epoll_wait), every idle sleep interval since
// last_sample_time_ has already completed by the time sampleUtilization(now) runs using the
// caller-provided snapshot timestamp now.
//
// Not thread-safe; dispatcher must outlive WorkerUtilizationTracker, and construction,
// destruction, and all method calls must occur on dispatcher's thread.
class WorkerUtilizationTracker final : public WorkerUtilizationTrackerInterface,
                                       public Event::Evwatch::Observer {
public:
  explicit WorkerUtilizationTracker(Event::Dispatcher& dispatcher);
  WorkerUtilizationTracker(const WorkerUtilizationTracker&) = delete;
  WorkerUtilizationTracker& operator=(const WorkerUtilizationTracker&) = delete;
  ~WorkerUtilizationTracker() override;

  // Event::Evwatch::Observer
  void onPrepare(MonotonicTime prepare_time,
                 std::optional<MonotonicTime::duration> timeout) override;
  void onCheck(MonotonicTime check_time) override;

  // WorkerUtilizationTrackerInterface
  double sampleUtilization(MonotonicTime now) override;

private:
  struct PendingPoll {
    MonotonicTime prepare_time;
    std::optional<MonotonicTime::duration> timeout;
  };

  Event::Dispatcher& dispatcher_;
  std::optional<PendingPoll> pending_poll_;
  MonotonicTime::duration accumulated_idle_time_{0};
  MonotonicTime last_sample_time_;
};

} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
