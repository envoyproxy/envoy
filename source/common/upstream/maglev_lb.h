#pragma once

#include "envoy/common/random_generator.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/upstream/thread_aware_lb_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * All Maglev load balancer stats. @see stats_macros.h
 */
#define ALL_MAGLEV_LOAD_BALANCER_STATS(GAUGE)                                                      \
  GAUGE(max_entries_per_host, Accumulate)                                                          \
  GAUGE(min_entries_per_host, Accumulate)

/**
 * Struct definition for all Maglev load balancer stats. @see stats_macros.h
 */
struct MaglevLoadBalancerStats {
  ALL_MAGLEV_LOAD_BALANCER_STATS(GENERATE_GAUGE_STRUCT)
};

/**
 * This is an implementation of Maglev consistent hashing as described in:
 * https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf
 * section 3.4. Specifically, the algorithm shown in pseudocode listing 1 is implemented with a
 * fixed table size of 65537. This is the recommended table size in section 5.3.
 */
class MaglevTable : public ThreadAwareLoadBalancerBase::HashingLoadBalancer,
                    Logger::Loggable<Logger::Id::upstream> {
public:
  MaglevTable(const NormalizedHostWeightVector& normalized_host_weights,
              double max_normalized_weight, uint64_t table_size, bool use_hostname_for_hashing,
              MaglevLoadBalancerStats& stats);

  // ThreadAwareLoadBalancerBase::HashingLoadBalancer
  HostConstSharedPtr chooseHost(uint64_t hash, uint32_t attempt) const override;

  // Recommended table size in section 5.3 of the paper.
  static const uint64_t DefaultTableSize = 65537;

private:
  struct TableBuildEntry {
    TableBuildEntry(const HostConstSharedPtr& host, uint64_t offset, uint64_t skip, double weight)
        : host_(host), offset_(offset), skip_(skip), weight_(weight) {}

    HostConstSharedPtr host_;
    const uint64_t offset_;
    const uint64_t skip_;
    const double weight_;
    double target_weight_{};
    uint64_t next_{};
    uint64_t count_{};
  };

  uint64_t permutation(const TableBuildEntry& entry);

  const uint64_t table_size_;
  std::vector<HostConstSharedPtr> table_;
  MaglevLoadBalancerStats& stats_;
};

/**
 * Thread aware load balancer implementation for Maglev.
 */
class MaglevLoadBalancer : public ThreadAwareLoadBalancerBase,
                           Logger::Loggable<Logger::Id::upstream> {
public:
  MaglevLoadBalancer(
      const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      const absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig>& config,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);

  const MaglevLoadBalancerStats& stats() const { return stats_; }
  uint64_t tableSize() const { return table_size_; }

private:
  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr
  createLoadBalancer(const NormalizedHostWeightVector& normalized_host_weights,
                     double /* min_normalized_weight */, double max_normalized_weight) override {
    HashingLoadBalancerSharedPtr maglev_lb =
        std::make_shared<MaglevTable>(normalized_host_weights, max_normalized_weight, table_size_,
                                      use_hostname_for_hashing_, stats_);

    if (hash_balance_factor_ == 0) {
      return maglev_lb;
    }

    return std::make_shared<BoundedLoadHashingLoadBalancer>(
        maglev_lb, std::move(normalized_host_weights), hash_balance_factor_);
  }

  static MaglevLoadBalancerStats generateStats(Stats::Scope& scope);

  Stats::ScopeSharedPtr scope_;
  MaglevLoadBalancerStats stats_;
  const uint64_t table_size_;
  const bool use_hostname_for_hashing_;
  const uint32_t hash_balance_factor_;
};

} // namespace Upstream
} // namespace Envoy
