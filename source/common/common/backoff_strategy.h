#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"

#include "common/common/assert.h"

namespace Envoy {

/**
 * Generic interface for all backoff strategy implementations.
 */
class BackOffStrategy {
public:
  virtual ~BackOffStrategy() {}

  /**
   * Returns the next backoff interval.
   */
  virtual std::uint64_t nextBackOff() PURE;

  /**
   * Resets the intervals so that the back off intervals can start again.
   */
  virtual void reset() PURE;
};

typedef std::unique_ptr<BackOffStrategy> BackOffStrategyPtr;

/**
 * Implementation of BackOffStrategy that increases the back off period for each retry attempt. 
 * When the interval has reached the max interval, it is no longer increased.
 */
class ExponentialBackOffStrategy : public BackOffStrategy {

public:
  ExponentialBackOffStrategy(const std::uint64_t initial_interval, const std::uint64_t max_interval,
                             const std::uint32_t multiplier);

  // BackOffStrategy methods
  uint64_t nextBackOff() override;
  void reset() override;

private:
  uint64_t computeNextInterval();
  uint64_t multiplyInterval();

  uint64_t initial_interval_;
  uint64_t max_interval_;
  uint32_t multiplier_;
  uint64_t current_interval_;
};
} // namespace Envoy
