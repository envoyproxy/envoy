#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
/**
 * Generic interface for all backoff strategy implementations.
 */
class BackOffStrategy {
public:
  virtual ~BackOffStrategy() = default;

  /**
   * @return the next backoff interval in milli seconds.
   */
  virtual uint64_t nextBackOffMs() PURE;

  /**
   * Resets the intervals so that the back off intervals can start again.
   */
  virtual void reset() PURE;

  /**
   * Resets the interval with a (potentially) new starting point.
   * @param base_interval the new base interval for the backoff strategy.
   */
  virtual void reset(uint64_t base_interval) PURE;
};

using BackOffStrategyPtr = std::unique_ptr<BackOffStrategy>;
} // namespace Envoy
