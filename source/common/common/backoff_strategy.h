#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/backoff_strategy.h"
#include "envoy/runtime/runtime.h"

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

/**
 * Implementation of BackOffStrategy that uses a fully jittered exponential backoff algorithm.
 */
class JitteredBackOffStrategy : public BackOffStrategy {

public:
  /**
   * Use this constructor if max_interval need not be enforced.
   * @param base_interval the base_interval to be used for next backoff computation.
   * @param random the random generator
   */
  JitteredBackOffStrategy(uint64_t base_interval, Runtime::RandomGenerator& random);

  /**
   * Use this constructor if max_interval need to be enforced.
   * @param base_interval the base_interval to be used for next backoff computation.
   * @param max_interval if the computed next backoff is more than this, this will be returned.
   * @param random the random generator
   */
  JitteredBackOffStrategy(uint64_t base_interval, uint64_t max_interval,
                          Runtime::RandomGenerator& random);

  // BackOffStrategy methods
  uint64_t nextBackOffMs() override;
  void reset() override;

private:
  uint64_t computeNextInterval();

  const uint64_t base_interval_;
  const uint64_t max_interval_{};
  uint32_t current_retry_{};
  Runtime::RandomGenerator& random_;
};
} // namespace Envoy
