#pragma once

#include "envoy/common/time.h"

#include "source/extensions/resource_monitors/adaptive_worker_capacity/dynamic_token_bucket.h"
#include "source/extensions/resource_monitors/adaptive_worker_capacity/worker_utilization_tracker.h"

#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace AdaptiveWorkerCapacity {

// Configuration options for RefillRateController.
struct RefillRateControllerOptions {
  absl::Duration refill_rate_adaptation_time;
  double target_worker_utilization{0.0};
  double min_bucket_utilization_for_rate_increase{0.0};
};

// RefillRateController adjusts a worker thread's DynamicTokenBucket refill rate based on worker
// loop (epoll) utilization and bucket utilization.
//
// Modes:
// - InitializeLimit: Initial mode. The token bucket starts uninitialized (refill_rate = 0,
//   reporting 0 pressure and 0 bucket utilization) and only counts accepted tokens. On the first
//   overload (worker_utilization > target_worker_utilization with last_consumption_rate > 0), snaps
//   the refill rate to last_consumption_rate and transitions to Normal.
// - Normal: Continuous log-linear adaptation around target_worker_utilization:
//   Normalized error is symmetric around target_worker_utilization:
//     normalized_error = min(1.0, |utilization - target| / (1 - target))
//   - If worker_utilization < target_worker_utilization AND
//       bucket_utilization > min_bucket_utilization_for_rate_increase:
//       Increases the refill rate exponentially (capped at 32x per update):
//         refill_rate *= 2 ^ (normalized_error * delta_time / adaptation_time)
//   - Else if worker_utilization > target_worker_utilization:
//       Decreases the refill rate exponentially:
//         refill_rate *= 0.5 ^ (normalized_error * delta_time / adaptation_time)
//   - Otherwise, no rate change is made.
class RefillRateController {
public:
  enum class Mode {
    InitializeLimit,
    Normal,
  };

  // Note: config must have a finite positive refill_rate_adaptation_time,
  // target_worker_utilization in (0.0, 1.0), and min_bucket_utilization_for_rate_increase in
  // [0.0, 1.0]; bucket, utilization_tracker, and time_source must outlive the controller.
  RefillRateController(const RefillRateControllerOptions& config, DynamicTokenBucket& bucket,
                       WorkerUtilizationTrackerInterface& utilization_tracker,
                       TimeSource& time_source);
  RefillRateController(const RefillRateController&) = delete;
  RefillRateController& operator=(const RefillRateController&) = delete;

  // Periodically invoked from the cold path (on the worker thread dispatcher) to capture a
  // single monotonic snapshot timestamp, evaluate worker/bucket utilization, and adjust the
  // bucket refill rate.
  void updateRefillRate();

  // Accessors for testing and introspection.
  const RefillRateControllerOptions& config() const { return config_; }
  Mode mode() const { return mode_; }
  double lastConsumptionRate() const { return last_consumption_rate_; }
  MonotonicTime lastUpdateTime() const { return last_update_time_; }
  double lastWorkerUtilization() const { return last_worker_utilization_; }
  double lastBucketUtilization() const { return last_bucket_utilization_; }

private:
  void updateRefillRateInitializeLimit(MonotonicTime now);
  void updateRefillRateNormal(absl::Duration delta_time, MonotonicTime now);
  void increaseRefillRate(absl::Duration delta_time, double normalized_error, MonotonicTime now);
  void decreaseRefillRate(absl::Duration delta_time, double normalized_error, MonotonicTime now);

  const RefillRateControllerOptions config_;
  DynamicTokenBucket& bucket_;
  WorkerUtilizationTrackerInterface& utilization_tracker_;
  TimeSource& time_source_;
  Mode mode_{Mode::InitializeLimit};
  double last_consumption_rate_{0.0};
  MonotonicTime last_update_time_;
  double last_worker_utilization_{0.0};
  double last_bucket_utilization_{0.0};
};

} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
