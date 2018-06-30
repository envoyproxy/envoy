#pragma once

#include "envoy/common/pure.h"

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
  virtual uint64_t nextBackOff() PURE;

  /**
   * Resets the intervals so that the back off intervals can start again.
   */
  virtual void reset() PURE;
};

typedef std::unique_ptr<BackOffStrategy> BackOffStrategyPtr;
} // namespace Envoy