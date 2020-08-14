#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/backoff_strategy.h"
#include "envoy/common/random_generator.h"

#include "common/common/assert.h"

namespace Envoy {

/**
 * Implementation of BackOffStrategy that uses a fully jittered exponential backoff algorithm.
 */
class JitteredExponentialBackOffStrategy : public BackOffStrategy {

public:
  /**
   * Constructs fully jittered backoff strategy.
   * @param base_interval the base interval to be used for next backoff computation. It should be
   * greater than zero and less than or equal to max_interval.
   * @param max_interval the cap on the next backoff value.
   * @param random the random generator.
   */
  JitteredExponentialBackOffStrategy(uint64_t base_interval, uint64_t max_interval,
                                     Random::RandomGenerator& random);

  // BackOffStrategy methods
  uint64_t nextBackOffMs() override;
  void reset() override;

private:
  const uint64_t base_interval_;
  const uint64_t max_interval_{};
  uint64_t next_interval_;
  Random::RandomGenerator& random_;
};

/**
 * Implementation of BackOffStrategy that returns random values in the range
 * [min_interval, 1.5 * min_interval).
 */
class JitteredLowerBoundBackOffStrategy : public BackOffStrategy {
public:
  /**
   * Constructs fully jittered backoff strategy.
   * @param min_interval the lower bound on the next backoff value.
   * @param random the random generator.
   */
  JitteredLowerBoundBackOffStrategy(uint64_t min_interval, Random::RandomGenerator& random);

  // BackOffStrategy methods
  uint64_t nextBackOffMs() override;
  void reset() override {}

private:
  const uint64_t min_interval_;
  Random::RandomGenerator& random_;
};

/**
 * Implementation of BackOffStrategy that uses a fixed backoff.
 */
class FixedBackOffStrategy : public BackOffStrategy {

public:
  /**
   * Constructs fixed backoff strategy.
   * @param interval_ms the fixed backoff duration. It should be greater than zero.
   */
  FixedBackOffStrategy(uint64_t interval_ms);

  // BackOffStrategy methods.
  uint64_t nextBackOffMs() override;
  void reset() override {}

private:
  const uint64_t interval_ms_;
};

} // namespace Envoy
