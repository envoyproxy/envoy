#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.h"
#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.validate.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/bit_array.h"
#include "source/extensions/load_balancing_policies/common/thread_aware_lb_impl.h"

namespace Envoy {
namespace Upstream {

using MaglevLbProto = envoy::extensions::load_balancing_policies::maglev::v3::Maglev;
using ClusterProto = envoy::config::cluster::v3::Cluster;
using LegacyMaglevLbProto = ClusterProto::MaglevLbConfig;

/**
 * Load balancer config that used to wrap legacy maglev config.
 */
class LegacyMaglevLbConfig : public Upstream::LoadBalancerConfig {
public:
  LegacyMaglevLbConfig(const ClusterProto& cluster);

  OptRef<const LegacyMaglevLbProto> lbConfig() const {
    if (lb_config_.has_value()) {
      return lb_config_.value();
    }
    return {};
  };

private:
  absl::optional<LegacyMaglevLbProto> lb_config_;
};

/**
 * Load balancer config that used to wrap typed maglev config.
 */
class TypedMaglevLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedMaglevLbConfig(const MaglevLbProto& config);

  const MaglevLbProto lb_config_;
};

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

class MaglevTable;
using MaglevTableSharedPtr = std::shared_ptr<MaglevTable>;

/**
 * This is an implementation of Maglev consistent hashing as described in:
 * https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf
 * section 3.4. Specifically, the algorithm shown in pseudocode listing 1 is implemented with a
 * fixed table size of 65537. This is the recommended table size in section 5.3.
 */
class MaglevTable : public ThreadAwareLoadBalancerBase::HashingLoadBalancer,
                    protected Logger::Loggable<Logger::Id::upstream> {
public:
  MaglevTable(uint64_t table_size, MaglevLoadBalancerStats& stats);
  ~MaglevTable() override = default;

  // Recommended table size in section 5.3 of the paper.
  static constexpr uint64_t DefaultTableSize = 65537;
  static constexpr uint64_t MaxNumberOfHostsForCompactMaglev = (static_cast<uint64_t>(1) << 32) - 1;

protected:
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

  /**
   * Template method for constructing the Maglev table.
   */
  void constructMaglevTableInternal(const NormalizedHostWeightVector& normalized_host_weights,
                                    double max_normalized_weight, bool use_hostname_for_hashing);

  const uint64_t table_size_;
  MaglevLoadBalancerStats& stats_;

private:
  /**
   * Implementation specific construction of data structures to represent the
   * Maglev Table.
   */
  virtual void constructImplementationInternals(std::vector<TableBuildEntry>& table_build_entries,
                                                double max_normalized_weight) PURE;

  /**
   * Log each entry of the maglev table (useful for debugging).
   */
  virtual void logMaglevTable(bool use_hostname_for_hashing) const PURE;
};

/**
 * This is an implementation of Maglev consistent hashing that directly holds
 * pointers.
 */
class OriginalMaglevTable : public MaglevTable {
public:
  OriginalMaglevTable(const NormalizedHostWeightVector& normalized_host_weights,
                      double max_normalized_weight, uint64_t table_size,
                      bool use_hostname_for_hashing, MaglevLoadBalancerStats& stats)
      : MaglevTable(table_size, stats) {
    constructMaglevTableInternal(normalized_host_weights, max_normalized_weight,
                                 use_hostname_for_hashing);
  }
  ~OriginalMaglevTable() override = default;

  // ThreadAwareLoadBalancerBase::HashingLoadBalancer
  HostConstSharedPtr chooseHost(uint64_t hash, uint32_t attempt) const override;

private:
  void constructImplementationInternals(std::vector<TableBuildEntry>& table_build_entries,
                                        double max_normalized_weight) override;

  void logMaglevTable(bool use_hostname_for_hashing) const override;
  std::vector<HostConstSharedPtr> table_;
};

/**
 * This maglev implementation leverages the number of hosts to more efficiently
 * populate the maglev table.
 * TODO(kbaichoo): Re-evaluate whether we should have the abstraction on the
 * table representation.
 */
class CompactMaglevTable : public MaglevTable {
public:
  CompactMaglevTable(const NormalizedHostWeightVector& normalized_host_weights,
                     double max_normalized_weight, uint64_t table_size,
                     bool use_hostname_for_hashing, MaglevLoadBalancerStats& stats);
  ~CompactMaglevTable() override = default;

  // ThreadAwareLoadBalancerBase::HashingLoadBalancer
  HostConstSharedPtr chooseHost(uint64_t hash, uint32_t attempt) const override;

private:
  void constructImplementationInternals(std::vector<TableBuildEntry>& table_build_entries,
                                        double max_normalized_weight) override;
  void logMaglevTable(bool use_hostname_for_hashing) const override;

  // Leverage a BitArray to more compactly fit represent the MaglevTable.
  // The BitArray will index into the host_table_ which will provide the given
  // host to load balance to.
  BitArray table_;
  std::vector<HostConstSharedPtr> host_table_;
};

/**
 * Thread aware load balancer implementation for Maglev.
 */
class MaglevLoadBalancer : public ThreadAwareLoadBalancerBase {
public:
  MaglevLoadBalancer(const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
                     Runtime::Loader& runtime, Random::RandomGenerator& random,
                     OptRef<const envoy::config::cluster::v3::Cluster::MaglevLbConfig> config,
                     const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);

  MaglevLoadBalancer(const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
                     Runtime::Loader& runtime, Random::RandomGenerator& random,
                     uint32_t healthy_panic_threshold,
                     const envoy::extensions::load_balancing_policies::maglev::v3::Maglev& config);

  const MaglevLoadBalancerStats& stats() const { return stats_; }
  uint64_t tableSize() const { return table_size_; }

private:
  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr
  createLoadBalancer(const NormalizedHostWeightVector& normalized_host_weights,
                     double /* min_normalized_weight */, double max_normalized_weight) override;
  static MaglevLoadBalancerStats generateStats(Stats::Scope& scope);

  Stats::ScopeSharedPtr scope_;
  MaglevLoadBalancerStats stats_;
  const uint64_t table_size_;
  const bool use_hostname_for_hashing_;
  const uint32_t hash_balance_factor_;
};

} // namespace Upstream
} // namespace Envoy
