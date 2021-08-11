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
};

using BackOffStrategyPtr = std::unique_ptr<BackOffStrategy>;
} // namespace Envoy
