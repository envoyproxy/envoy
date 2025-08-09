#include "contrib/peak_ewma/load_balancing_policies/source/observability.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

GlobalHostStats::GlobalHostStats(Upstream::HostConstSharedPtr host, Stats::Scope& scope,
                                 TimeSource& /* time_source */)
    : cost_stat_(scope.gaugeFromString("peak_ewma." + host->address()->asString() + ".cost",
                                       Stats::Gauge::ImportMode::NeverImport)),
      ewma_rtt_stat_(
          scope.gaugeFromString("peak_ewma." + host->address()->asString() + ".ewma_rtt_ms",
                                Stats::Gauge::ImportMode::NeverImport)),
      active_requests_stat_(
          scope.gaugeFromString("peak_ewma." + host->address()->asString() + ".active_requests",
                                Stats::Gauge::ImportMode::NeverImport)),
      host_(host) {}

void GlobalHostStats::setComputedCostStat(double cost) {
  cost_stat_.set(static_cast<uint64_t>(cost));
}

void GlobalHostStats::setEwmaRttStat(double ewma_rtt_ms) {
  ewma_rtt_stat_.set(static_cast<uint64_t>(ewma_rtt_ms));
}

void GlobalHostStats::setActiveRequestsStat(double active_requests) {
  active_requests_stat_.set(static_cast<uint64_t>(active_requests));
}

void Observability::report(
    const absl::flat_hash_map<Upstream::HostConstSharedPtr,
                              std::unique_ptr<GlobalHostStats>>& /* all_host_stats */) {
  // Stats are published during aggregation - this is a placeholder for consistency
}

std::unique_ptr<GlobalHostStats> Observability::createHostStats(Upstream::HostConstSharedPtr host) {
  return std::make_unique<GlobalHostStats>(host, scope_, time_source_);
}

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
