#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
/**
 * Generic interface for all MutexTracer implementations. Any MutexTracer should initialize itself
 * with absl::RegisterMutexTracer() at initialization, record statistics, and deliver those
 * statistics in a thread-safe manner.
 */
class MutexTracer {
public:
  virtual ~MutexTracer() {}

  /**
   * @return resets the captured statistics.
   */
  virtual void reset() PURE;

  /**
   * @return the number of experienced mutex contentions.
   */
  virtual int64_t numContentions() const PURE;

  /**
   * @return the length of the ongoing wait cycle. Note that the wait cycles are not
   * guaranteed to correspond to core clock frequency, as per absl::base_internal::CycleClock.
   */
  virtual int64_t currentWaitCycles() const PURE;

  /**
   * @return the cumulative length of all experienced wait cycles. See the above note on wait cycles
   * v. core clock frequency.
   */
  virtual int64_t lifetimeWaitCycles() const PURE;
};

} // namespace Envoy
