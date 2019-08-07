#pragma once

#include <chrono>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {

/**
 * The controller's decision on whether a request will be forwarded.
 */
enum class RequestForwardingAction {
  // The concurrency limit is exceeded, so the request cannot be forwarded.
  Block,

  // The controller has allowed the request through and changed its internal
  // state. The request must be forwarded.
  Forward
};

/**
 * Adaptive concurrency controller interface. All implementations of this
 * interface must be thread-safe.
 */
class ConcurrencyController {
public:
  virtual ~ConcurrencyController() = default;

  /**
   * Called during decoding when the adaptive concurrency filter is attempting
   * to forward a request. Returns its decision on whether to forward a request.
   */
  virtual RequestForwardingAction forwardingDecision() PURE;

  /**
   * Called during encoding when the request latency is known. Records the
   * request latency to update the internal state of the controller for
   * concurrency limit calculations.
   *
   * @param rq_latency is the clocked round-trip time for the request.
   */
  virtual void recordLatencySample(const std::chrono::nanoseconds& rq_latency) PURE;
};

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
