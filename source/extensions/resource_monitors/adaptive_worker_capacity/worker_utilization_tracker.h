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

  // Calculates thread utilization in [0.0, 1.0] since the last time it was sampled.
  virtual double sampleUtilization() = 0;
};

// WorkerUtilizationTracker measures event-loop CPU utilization on a worker thread.
//
// This tracker is designed for persistent blocking worker event loops (RunType::Block or
// RunType::RunUntilExit), not episodic RunType::NonBlock loops (which exit libevent after
// EVWATCH_CHECK without a closing EVWATCH_PREPARE).
//
// Not thread-safe; construction, destruction, and all method calls must occur on dispatcher's
// thread.
//
// Both dispatcher and time_source must outlive WorkerUtilizationTracker, and time_source must match
// the monotonic clock backing dispatcher.
class WorkerUtilizationTracker final : public WorkerUtilizationTrackerInterface,
                                       public Event::Evwatch::Observer {
public:
  WorkerUtilizationTracker(Event::Dispatcher& dispatcher, TimeSource& time_source);
  WorkerUtilizationTracker(const WorkerUtilizationTracker&) = delete;
  WorkerUtilizationTracker& operator=(const WorkerUtilizationTracker&) = delete;
  ~WorkerUtilizationTracker() override;

  // Event::Evwatch::Observer
  void onPrepare(MonotonicTime prepare_time,
                 std::optional<MonotonicTime::duration> timeout) override;
  void onCheck(MonotonicTime check_time) override;

  // WorkerUtilizationTrackerInterface
  double sampleUtilization() override;

private:
  Event::Dispatcher& dispatcher_;
  TimeSource& time_source_;
  std::optional<MonotonicTime> busy_start_time_;
  MonotonicTime::duration accumulated_busy_time_{0};
  MonotonicTime last_poll_time_;
};

} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
