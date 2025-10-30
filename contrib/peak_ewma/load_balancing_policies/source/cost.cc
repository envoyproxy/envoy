#include "contrib/peak_ewma/load_balancing_policies/source/cost.h"

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

double Cost::compute(double rtt_ewma_ms, double active_requests, double default_rtt_ms) const {
  const bool has_rtt = (rtt_ewma_ms > 0.0);
  const bool has_requests = (active_requests > 0.0);

  if (!has_rtt && has_requests) {
    // Host has requests but no RTT data - likely failing, penalize heavily
    return penalty_value_ + active_requests;
  } else if (has_rtt) {
    // Standard Peak EWMA formula: cost = latency * load
    return rtt_ewma_ms * (active_requests + 1.0);
  } else {
    // No RTT and no requests: treat as having default RTT performance
    return default_rtt_ms * (active_requests + 1.0);
  }
}

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
