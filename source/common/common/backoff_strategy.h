#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/backoff_strategy.h"
#include "envoy/runtime/runtime.h"

#include "common/common/assert.h"

namespace Envoy {

/**
 * Implementation of BackOffStrategy that uses a fully jittered exponential backoff algorithm.
 */
class JitteredBackOffStrategy : public BackOffStrategy {

public:
  /**
   * Constructs fully jittered backoff strategy.
   * @param base_interval the base_interval to be used for next backoff computation. It should be
   * greater than zero and less than or equal to max_interval.
   * @param max_interval the cap on the next backoff value.
   * @param random the random generator
   */
  JitteredBackOffStrategy(uint64_t base_interval, uint64_t max_interval,
                          Runtime::RandomGenerator& random);

  // BackOffStrategy methods
  uint64_t nextBackOffMs() override;
  void reset() override;

private:
  const uint64_t base_interval_;
  const uint64_t max_interval_{};
  uint64_t current_retry_{1};
  Runtime::RandomGenerator& random_;
};
} // namespace Envoy
