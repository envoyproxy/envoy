#pragma once

#include <memory>

#include "envoy/common/time.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "absl/container/flat_hash_map.h"
#include "contrib/peak_ewma/load_balancing_policies/source/cost.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

/**
 * Manages stats for a single host, publishing EWMA RTT, active requests, and computed cost
 * to the admin interface for observability.
 */
struct GlobalHostStats {
  GlobalHostStats(Upstream::HostConstSharedPtr host, Stats::Scope& scope, TimeSource& time_source);

  void setComputedCostStat(double cost);
  void setEwmaRttStat(double ewma_rtt_ms);
  void setActiveRequestsStat(double active_requests);

private:
  Stats::Gauge& cost_stat_;
  Stats::Gauge& ewma_rtt_stat_;
  Stats::Gauge& active_requests_stat_;
  Upstream::HostConstSharedPtr host_;
};

/**
 * Peak EWMA observability and metrics reporting.
 * Creates per-host stats showing EWMA RTT, active requests, and computed costs.
 */
class Observability {
public:
  Observability(Stats::Scope& scope, TimeSource& time_source, const Cost& cost_calculator,
                double default_rtt_ms)
      : scope_(scope), time_source_(time_source), cost_calculator_(cost_calculator),
        default_rtt_ms_(default_rtt_ms) {}

  /**
   * Report host metrics for admin interface visibility.
   * Called after EWMA aggregation to update per-host metrics.
   */
  void report(const absl::flat_hash_map<Upstream::HostConstSharedPtr,
                                        std::unique_ptr<GlobalHostStats>>& all_host_stats);

  /**
   * Create stats object for a new host.
   */
  std::unique_ptr<GlobalHostStats> createHostStats(Upstream::HostConstSharedPtr host);

private:
  Stats::Scope& scope_;
  TimeSource& time_source_;
  [[maybe_unused]] const Cost& cost_calculator_;
  [[maybe_unused]] const double default_rtt_ms_;
};

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
