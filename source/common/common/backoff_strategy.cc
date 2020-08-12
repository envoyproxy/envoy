#include "common/common/backoff_strategy.h"

namespace Envoy {

JitteredBackOffStrategy::JitteredBackOffStrategy(uint64_t base_interval, uint64_t max_interval,
                                                 Random::RandomGenerator& random)
    : base_interval_(base_interval), max_interval_(max_interval), next_interval_(base_interval),
      random_(random) {
  ASSERT(base_interval_ > 0);
  ASSERT(base_interval_ <= max_interval_);
}

uint64_t JitteredBackOffStrategy::nextBackOffMs() {
  const uint64_t backoff = next_interval_;
  ASSERT(backoff > 0);
  // Set next_interval_ to max_interval_ if doubling the interval would exceed the max or overflow.
  if (next_interval_ < max_interval_ / 2) {
    next_interval_ *= 2;
  } else {
    next_interval_ = max_interval_;
  }
  return std::min(random_.random() % backoff, max_interval_);
}

void JitteredBackOffStrategy::reset() { next_interval_ = base_interval_; }

FixedBackOffStrategy::FixedBackOffStrategy(uint64_t interval_ms) : interval_ms_(interval_ms) {
  ASSERT(interval_ms_ > 0);
}

uint64_t FixedBackOffStrategy::nextBackOffMs() { return interval_ms_; }

} // namespace Envoy
