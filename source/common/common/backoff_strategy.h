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
  const uint64_t base_interval_;
  const uint64_t max_interval_{};
  uint64_t current_retry_{1};
  Runtime::RandomGenerator& random_;
};
} // namespace Envoy
