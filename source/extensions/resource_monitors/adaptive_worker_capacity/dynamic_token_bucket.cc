#include "source/extensions/resource_monitors/adaptive_worker_capacity/dynamic_token_bucket.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace AdaptiveWorkerCapacity {

namespace {

constexpr double kMinRefillRate = 1e-3;
constexpr double kMaxRefillRate = 1e12;
constexpr double kMinMaxTokens = 1.0;
constexpr double kMaxTokens = 1e12;

} // namespace

DynamicTokenBucket::DynamicTokenBucket(TimeSource& time_source, absl::Duration bucket_size)
    : time_source_(time_source), bucket_size_(bucket_size),
      last_token_update_time_(time_source.monotonicTime()),
      last_utilization_pop_time_(last_token_update_time_),
      last_consumption_rate_pop_time_(last_token_update_time_) {
  ASSERT(bucket_size > absl::ZeroDuration() && bucket_size < absl::InfiniteDuration());
}

void DynamicTokenBucket::refill(MonotonicTime now) {
  if (now <= last_token_update_time_) {
    return;
  }
  const double dt = std::chrono::duration<double>(now - last_token_update_time_).count();
  last_token_update_time_ = now;

  if (refill_rate_ <= 0.0 || !max_tokens_.has_value()) {
    return;
  }
  const double max_tokens = *max_tokens_;

  const double refilled_tokens = std::min(max_tokens, token_count_ + dt * refill_rate_);

  if (token_count_ + dt * refill_rate_ <= max_tokens) {
    const double avg_tokens = (token_count_ + refilled_tokens) / 2.0;
    accumulated_area_ += dt * (max_tokens - avg_tokens);
  } else {
    const double time_to_full = std::max(0.0, (max_tokens - token_count_) / refill_rate_);
    const double avg_tokens = (token_count_ + max_tokens) / 2.0;
    accumulated_area_ += time_to_full * (max_tokens - avg_tokens);
  }

  token_count_ = refilled_tokens;
}

Server::ResourceUsage DynamicTokenBucket::getResourceUsage() {
  if (!max_tokens_.has_value()) {
    return Server::ResourceUsage{0.0};
  }
  refill(time_source_.monotonicTime());
  if (token_count_ < 1.0) {
    return Server::ResourceUsage{1.0};
  }
  const double consumed_fraction = (*max_tokens_ - token_count_) / *max_tokens_;
  return Server::ResourceUsage{std::clamp(consumed_fraction, 0.0, 1.0)};
}

void DynamicTokenBucket::onLoadAccepted() {
  if (max_tokens_.has_value()) {
    token_count_ = std::max(0.0, token_count_ - 1.0);
  }
  consumed_tokens_since_last_pop_ += 1.0;
}

double DynamicTokenBucket::popBucketUtilization(MonotonicTime now) {
  if (now <= last_utilization_pop_time_) {
    return 0.0;
  }
  refill(now);
  const double dt = std::chrono::duration<double>(now - last_utilization_pop_time_).count();
  last_utilization_pop_time_ = now;

  double utilization = 0.0;
  if (max_tokens_.has_value()) {
    utilization = accumulated_area_ / (dt * *max_tokens_);
  }
  accumulated_area_ = 0.0;
  return std::clamp(utilization, 0.0, 1.0);
}

double DynamicTokenBucket::popTokenConsumptionRate(MonotonicTime now) {
  if (now <= last_consumption_rate_pop_time_) {
    return 0.0;
  }
  const double dt = std::chrono::duration<double>(now - last_consumption_rate_pop_time_).count();
  last_consumption_rate_pop_time_ = now;

  const double rate = consumed_tokens_since_last_pop_ / dt;
  consumed_tokens_since_last_pop_ = 0.0;
  return rate;
}

void DynamicTokenBucket::setRefillRate(double new_refill_rate, MonotonicTime now) {
  if (new_refill_rate <= 0.0 || !std::isfinite(new_refill_rate)) {
    return;
  }
  refill(now);
  const std::optional<double> old_max_tokens = max_tokens_;
  refill_rate_ = std::clamp(new_refill_rate, kMinRefillRate, kMaxRefillRate);
  const double bucket_size_s = absl::ToDoubleSeconds(bucket_size_);
  max_tokens_ = std::clamp(bucket_size_s * refill_rate_, kMinMaxTokens, kMaxTokens);
  if (old_max_tokens.has_value()) {
    if (token_count_ >= *old_max_tokens) {
      token_count_ = *max_tokens_;
    } else {
      token_count_ = std::clamp((token_count_ * *max_tokens_) / *old_max_tokens, 0.0, *max_tokens_);
    }
    accumulated_area_ = (accumulated_area_ * *max_tokens_) / *old_max_tokens;
  } else {
    token_count_ = *max_tokens_;
    last_utilization_pop_time_ = last_token_update_time_;
  }
}

void DynamicTokenBucket::scaleRefillRate(double factor, MonotonicTime now) {
  if (factor <= 0.0 || !std::isfinite(factor) || refill_rate_ <= 0.0) {
    return;
  }
  setRefillRate(std::clamp(refill_rate_ * factor, kMinRefillRate, kMaxRefillRate), now);
}

} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
