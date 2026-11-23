#pragma once

#include <optional>

#include "envoy/common/time.h"
#include "envoy/server/resource_monitor.h"

#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace AdaptiveWorkerCapacity {

// DynamicTokenBucket represents a thread-local dynamic token bucket on a worker thread.
//
// Unlike TokenBucketImpl (which enforces a fixed fill rate and integer max_tokens via a
// single-step consume() call), DynamicTokenBucket is purpose-built for
// SynchronousFeedbackResourceMonitor:
// - Separates non-consuming pressure reads (getResourceUsage()) from post-trigger token deduction
//   (onLoadAccepted()) with a single clock read.
// - Supports continuous refill_rate_ and max_tokens_ scaling (clamped to >= 1.0) while preserving
//   the bucket's fill fraction.
// - Integrates time-averaged bucket depletion (popBucketUtilization()) during refills for
//   controller anti-windup.
// - Starts in an uninitialized cold-start state (max_tokens_ == std::nullopt) that tracks token
//   consumption rate with zero hot-path clock overhead.
//
// In the hot path:
// - getResourceUsage() returns the fraction of burst capacity currently consumed by this worker:
//   (max_tokens - token_count) / max_tokens in [0.0, 1.0] (or 1.0 if token_count < 1.0, and 0.0
//   when uninitialized).
// - onLoadAccepted() deducts tokens on the hot path.
//
// In the cold path (taking a caller-provided monotonic snapshot timestamp now):
// - popBucketUtilization(now) computes the average fraction of consumed tokens over the time
//   elapsed since the last pop:
//     utilization = accumulated_area / (delta_time * max_tokens)
//   and resets the accumulated area.
// - popTokenConsumptionRate(now) computes the rate of accepted tokens (tokens/sec) over the time
//   elapsed since the last pop and resets the count.
// - setRefillRate(new_refill_rate, now) directly sets the token refill rate, updating burst
//   capacity.
// - scaleRefillRate(factor, now) dynamically scales the refill rate and recomputes the burst
//   capacity (max_tokens = bucket_size * refill_rate).
class DynamicTokenBucket {
public:
  // Note: time_source must outlive the bucket, and bucket_size must be finite and positive.
  DynamicTokenBucket(TimeSource& time_source, absl::Duration bucket_size);
  DynamicTokenBucket(const DynamicTokenBucket&) = delete;
  DynamicTokenBucket& operator=(const DynamicTokenBucket&) = delete;

  // Synchronously evaluates and returns the consumed fraction of burst capacity (or 1.0 if fewer
  // than 1.0 tokens remain, and 0.0 when uninitialized). Always in [0.0, 1.0].
  Server::ResourceUsage getResourceUsage();

  // Called when load is accepted by a LoadShedPoint without shedding. Deducts a token from the
  // bucket. Note that refill() (called during getResourceUsage() and cold-path updates)
  // accumulates the consumed capacity area; onLoadAccepted() assumes getResourceUsage() was just
  // evaluated on the hot path by LoadShedPoint.
  void onLoadAccepted();

  // Returns the bucket utilization over the interval ending at now (a monotonic timestamp obtained
  // from time_source) since the previous sample and resets the accumulated area. Range [0.0, 1.0].
  double popBucketUtilization(MonotonicTime now);

  // Returns the rate of accepted tokens (tokens/sec) over the interval ending at now (a monotonic
  // timestamp obtained from time_source) since the previous sample and resets the count.
  double popTokenConsumptionRate(MonotonicTime now);

  // Directly sets the token refill rate (tokens/sec) at monotonic timestamp now, updating burst
  // capacity.
  void setRefillRate(double new_refill_rate, MonotonicTime now);

  // Scales the token refill rate by factor (> 0.0) at monotonic timestamp now, updating burst
  // capacity.
  void scaleRefillRate(double factor, MonotonicTime now);

  // Accessors for inspection and testing.
  double refillRate() const { return refill_rate_; }
  std::optional<double> maxTokens() const { return max_tokens_; }
  double tokenCount() const { return token_count_; }
  absl::Duration bucketSize() const { return bucket_size_; }

private:
  void refill(MonotonicTime now);

  TimeSource& time_source_;
  const absl::Duration bucket_size_;
  double refill_rate_{0.0};
  std::optional<double> max_tokens_;
  double token_count_{0.0};
  double accumulated_area_{0.0};
  double consumed_tokens_since_last_pop_{0.0};
  MonotonicTime last_token_update_time_;
  MonotonicTime last_utilization_pop_time_;
  MonotonicTime last_consumption_rate_pop_time_;
};

} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
