#pragma once

#include <chrono>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {

/**
 * Adaptive concurrency controller interface.
 */
class ConcurrencyController {
public:
  virtual ~ConcurrencyController() = default;

  /**
   * Called during decoding when the adaptive concurrency filter is attempting
   * to forward a request. Returns true if a request is successfully forwarded.
   */
  virtual bool tryForwardRequest() PURE;

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
