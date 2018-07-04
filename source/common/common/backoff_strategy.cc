#include "common/common/backoff_strategy.h"

namespace Envoy {

ExponentialBackOffStrategy::ExponentialBackOffStrategy(uint64_t initial_interval,
                                                       uint64_t max_interval, double multiplier)
    : initial_interval_(initial_interval), max_interval_(max_interval), multiplier_(multiplier),
      current_interval_(0) {
  ASSERT(multiplier_ > 1.0);
  ASSERT(initial_interval_ <= max_interval_);
  ASSERT(initial_interval_ * multiplier_ <= max_interval_);
}

uint64_t ExponentialBackOffStrategy::nextBackOffMs() { return computeNextInterval(); }

void ExponentialBackOffStrategy::reset() { current_interval_ = 0; }

uint64_t ExponentialBackOffStrategy::computeNextInterval() {
  if (current_interval_ == 0) {
    current_interval_ = initial_interval_;
  } else if (current_interval_ >= max_interval_) {
    current_interval_ = max_interval_;
  } else {
    uint64_t new_interval = current_interval_;
    new_interval = ceil(new_interval * multiplier_);
    current_interval_ = new_interval > max_interval_ ? max_interval_ : new_interval;
  }
  return current_interval_;
}

JitteredBackOffStrategy::JitteredBackOffStrategy(uint64_t base_interval,
                                                 Runtime::RandomGenerator& random)
    : base_interval_(base_interval), random_(random) {}

JitteredBackOffStrategy::JitteredBackOffStrategy(uint64_t base_interval, uint64_t max_interval,
                                                 Runtime::RandomGenerator& random)
    : base_interval_(base_interval), max_interval_(max_interval), random_(random) {
  ASSERT(base_interval_ < max_interval_);
}

uint64_t JitteredBackOffStrategy::nextBackOffMs() { return computeNextInterval(); }

void JitteredBackOffStrategy::reset() { current_retry_ = 0; }

uint64_t JitteredBackOffStrategy::computeNextInterval() {
  current_retry_++;
  uint32_t multiplier = (1 << current_retry_) - 1;
  uint64_t new_interval = random_.random() % (base_interval_ * multiplier);
  return (max_interval_ != 0 && new_interval > max_interval_) ? max_interval_ : new_interval;
}

} // namespace Envoy