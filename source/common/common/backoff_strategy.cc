#include "common/common/backoff_strategy.h"

namespace Envoy {

JitteredBackOffStrategy::JitteredBackOffStrategy(uint64_t base_interval, uint64_t max_interval,
                                                 Runtime::RandomGenerator& random)
    : base_interval_(base_interval), max_interval_(max_interval), random_(random) {
  ASSERT(base_interval_ <= max_interval_);
}

uint64_t JitteredBackOffStrategy::nextBackOffMs() {
  current_retry_++;
  uint32_t multiplier = (1 << current_retry_) - 1;
  if (multiplier == 0) {
    current_retry_ = 1;
    multiplier = (1 << current_retry_) - 1;
  }
  return std::min(random_.random() % (base_interval_ * multiplier), max_interval_);
}

void JitteredBackOffStrategy::reset() { current_retry_ = 0; }

} // namespace Envoy