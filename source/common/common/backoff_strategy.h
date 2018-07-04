#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/backoff_strategy.h"

#include "common/common/assert.h"

namespace Envoy {

/**
 * Implementation of BackOffStrategy that increases the back off period for each retry attempt. When
 * the interval has reached the max interval, it is no longer increased.
 */
class ExponentialBackOffStrategy : public BackOffStrategy {

public:
  ExponentialBackOffStrategy(uint64_t initial_interval, uint64_t max_interval, double multiplier);

  // BackOffStrategy methods
  uint64_t nextBackOffMs() override;
  void reset() override;

private:
  uint64_t computeNextInterval();

  const uint64_t initial_interval_;
  const uint64_t max_interval_;
  const double multiplier_;
  uint64_t current_interval_;
};
} // namespace Envoy
