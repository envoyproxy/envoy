#include "source/extensions/resource_monitors/adaptive_worker_capacity/refill_rate_controller.h"

#include <algorithm>
#include <cmath>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace AdaptiveWorkerCapacity {

RefillRateController::RefillRateController(const RefillRateControllerOptions& config,
                                           DynamicTokenBucket& bucket,
                                           WorkerUtilizationTrackerInterface& utilization_tracker,
                                           TimeSource& time_source)
    : config_(config), bucket_(bucket), utilization_tracker_(utilization_tracker),
      time_source_(time_source), last_update_time_(time_source.monotonicTime()) {
  ASSERT(config_.refill_rate_adaptation_time > absl::ZeroDuration() &&
         config_.refill_rate_adaptation_time < absl::InfiniteDuration());
  ASSERT(config_.target_worker_utilization > 0.0 && config_.target_worker_utilization < 1.0);
  ASSERT(config_.min_bucket_utilization_for_rate_increase >= 0.0 &&
         config_.min_bucket_utilization_for_rate_increase <= 1.0);
}

void RefillRateController::updateRefillRate() {
  const MonotonicTime now = time_source_.monotonicTime();
  const absl::Duration time_since_last_update = absl::FromChrono(now - last_update_time_);
  if (time_since_last_update <= absl::ZeroDuration()) {
    return;
  }
  last_update_time_ = now;

  last_worker_utilization_ = utilization_tracker_.sampleUtilization(now);
  last_bucket_utilization_ = bucket_.popBucketUtilization(now);
  last_consumption_rate_ = bucket_.popTokenConsumptionRate(now);

  switch (mode_) {
  case Mode::InitializeLimit:
    updateRefillRateInitializeLimit(now);
    break;
  case Mode::Normal:
    updateRefillRateNormal(time_since_last_update, now);
    break;
  }
}

void RefillRateController::updateRefillRateInitializeLimit(MonotonicTime now) {
  if (last_worker_utilization_ > config_.target_worker_utilization &&
      last_consumption_rate_ > 0.0) {
    bucket_.setRefillRate(last_consumption_rate_, now);
    mode_ = Mode::Normal;
  }
}

void RefillRateController::updateRefillRateNormal(absl::Duration delta_time, MonotonicTime now) {
  const double target = config_.target_worker_utilization;
  const double normalized_error =
      std::clamp(std::abs(last_worker_utilization_ - target) / (1.0 - target), 0.0, 1.0);
  if (last_worker_utilization_ < target &&
      last_bucket_utilization_ > config_.min_bucket_utilization_for_rate_increase) {
    increaseRefillRate(delta_time, normalized_error, now);
  } else if (last_worker_utilization_ > target) {
    decreaseRefillRate(delta_time, normalized_error, now);
  }
}

void RefillRateController::increaseRefillRate(absl::Duration delta_time, double normalized_error,
                                              MonotonicTime now) {
  ASSERT(normalized_error > 0.0);
  // Cap at 2^5 = 32x per update before exp2 to avoid IEEE-754 double overflow on long intervals.
  constexpr double kMaxIncreaseExponent = 5.0;
  const double exponent = std::min(
      kMaxIncreaseExponent,
      normalized_error * absl::FDivDuration(delta_time, config_.refill_rate_adaptation_time));
  bucket_.scaleRefillRate(std::exp2(exponent), now);
}

void RefillRateController::decreaseRefillRate(absl::Duration delta_time, double normalized_error,
                                              MonotonicTime now) {
  ASSERT(normalized_error > 0.0);
  // Floor above IEEE-754 double underflow (2^-64 spans DynamicTokenBucket's full [1e-3, 1e12]
  // rate range) so exp2 does not underflow to 0.0 and trigger the factor <= 0.0 early return in
  // scaleRefillRate.
  constexpr double kMinDecreaseExponent = -64.0;
  const double exponent = std::max(
      kMinDecreaseExponent,
      -normalized_error * absl::FDivDuration(delta_time, config_.refill_rate_adaptation_time));
  bucket_.scaleRefillRate(std::exp2(exponent), now);
}

} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
