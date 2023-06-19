#include "source/common/common/backoff_strategy.h"

namespace Envoy {

JitteredExponentialBackOffStrategy::JitteredExponentialBackOffStrategy(
    uint64_t base_interval, uint64_t max_interval, Random::RandomGenerator& random)
    : base_interval_(base_interval), max_interval_(max_interval), next_interval_(base_interval),
      random_(random) {
  ASSERT(base_interval_ > 0);
  ASSERT(base_interval_ <= max_interval_);
}

uint64_t JitteredExponentialBackOffStrategy::nextBackOffMs() {
  const uint64_t backoff = next_interval_;
  ASSERT(backoff > 0);
  // Set next_interval_ to max_interval_ if doubling the interval would exceed the max or overflow.
  next_interval_ = (next_interval_ < doubling_limit_) ? (next_interval_ * 2u) : max_interval_;
  return (random_.random() % backoff);
}

void JitteredExponentialBackOffStrategy::reset() { next_interval_ = base_interval_; }

JitteredLowerBoundBackOffStrategy::JitteredLowerBoundBackOffStrategy(
    uint64_t min_interval, Random::RandomGenerator& random)
    : min_interval_(min_interval), random_(random) {
  ASSERT(min_interval_ > 1);
}

uint64_t JitteredLowerBoundBackOffStrategy::nextBackOffMs() {
  // random(min_interval_, 1.5 * min_interval_)
  return (random_.random() % (min_interval_ >> 1)) + min_interval_;
}

FixedBackOffStrategy::FixedBackOffStrategy(uint64_t interval_ms) : interval_ms_(interval_ms) {
  ASSERT(interval_ms_ > 0);
}

uint64_t FixedBackOffStrategy::nextBackOffMs() { return interval_ms_; }

} // namespace Envoy
