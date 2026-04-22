#pragma once

#include <cstdint>
#include <utility>

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

/**
 * Peak EWMA cost calculation for host selection.
 * Encapsulates the cost function business logic with zero dependencies.
 */
class Cost {
public:
  /**
   * Constructor with configurable penalty value.
   * @param penalty_value Cost penalty for hosts with no RTT data
   */
  explicit Cost(double penalty_value = 1000000.0) : penalty_value_(penalty_value) {}

  /**
   * Compute cost for host selection using Peak EWMA algorithm.
   * Formula: cost = rtt_ewma * (active_requests + 1)
   *
   * @param rtt_ewma_ms EWMA RTT in milliseconds (0.0 if no data available)
   * @param active_requests Current active request count
   * @param default_rtt_ms Default RTT to use when no EWMA data available
   * @return Computed cost for P2C selection (lower is better)
   */
  double compute(double rtt_ewma_ms, double active_requests, double default_rtt_ms) const;

private:
  const double penalty_value_;
};

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
