#include "common/common/backoff_strategy.h"

namespace Envoy {

JitteredBackOffStrategy::JitteredBackOffStrategy(uint64_t base_interval, uint64_t max_interval,
                                                 Runtime::RandomGenerator& random)
    : base_interval_(base_interval), max_interval_(max_interval), random_(random) {
  ASSERT(base_interval_ > 0);
  ASSERT(base_interval_ <= max_interval_);
}

uint64_t JitteredBackOffStrategy::nextBackOffMs() {
  const uint64_t multiplier = (1 << current_retry_) - 1;
  const uint64_t base_backoff = multiplier * base_interval_;
  if (base_backoff <= max_interval_) {
    current_retry_++;
  }
  ASSERT(base_backoff > 0);
  return std::min(random_.random() % base_backoff, max_interval_);
}

void JitteredBackOffStrategy::reset() { current_retry_ = 1; }

FixedBackOffStrategy::FixedBackOffStrategy(uint64_t interval_ms) : interval_ms_(interval_ms) {
  ASSERT(interval_ms_ > 0);
}

uint64_t FixedBackOffStrategy::nextBackOffMs() { return interval_ms_; }

} // namespace Envoy
