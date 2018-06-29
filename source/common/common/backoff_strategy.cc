#include "common/common/backoff_strategy.h"

namespace Envoy {

ExponentialBackOffStrategy::ExponentialBackOffStrategy(const std::uint64_t initial_interval,
                                                       const std::uint64_t max_interval,
                                                       const std::uint32_t multiplier)
    : initial_interval_(initial_interval), max_interval_(max_interval), multiplier_(multiplier),
      current_interval_(0) {
  ASSERT(!(multiplier_ <= 0));
  ASSERT(initial_interval_ <= max_interval_);
}

uint64_t ExponentialBackOffStrategy::nextBackOff() { return computeNextInterval(); }

void ExponentialBackOffStrategy::reset() { current_interval_ = 0; }

uint64_t ExponentialBackOffStrategy::computeNextInterval() {
  if (current_interval_ == 0) {
    current_interval_ = initial_interval_;
  } else if (current_interval_ >= max_interval_) {
    current_interval_ = max_interval_;
  } else {
    current_interval_ = multiplyInterval();
  }
  return current_interval_;
}

uint64_t ExponentialBackOffStrategy::multiplyInterval() {
  uint64_t temp_interval = current_interval_;
  temp_interval *= multiplier_;
  return (temp_interval > max_interval_ ? max_interval_ : temp_interval);
}

} // namespace Envoy