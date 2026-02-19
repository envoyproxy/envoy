#pragma once

#include <cstdint>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/load_balancing_policies/rendezvous_hash/v3/rendezvous_hash.pb.h"
#include "envoy/extensions/load_balancing_policies/rendezvous_hash/v3/rendezvous_hash.pb.validate.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/load_balancer.h"

#include "source/extensions/load_balancing_policies/common/thread_aware_lb_impl.h"

namespace Envoy {
namespace Upstream {

// Forward declaration for test access.
class RendezvousHashMathTest;

using RendezvousHashLbProto =
    envoy::extensions::load_balancing_policies::rendezvous_hash::v3::RendezvousHash;
using ClusterProto = envoy::config::cluster::v3::Cluster;
using CommonLbConfigProto = envoy::config::cluster::v3::Cluster::CommonLbConfig;

/**
 * Load balancer config that wraps typed rendezvous hash config.
 */
class TypedRendezvousHashLbConfig : public Upstream::TypedHashLbConfigBase {
public:
  TypedRendezvousHashLbConfig(const RendezvousHashLbProto& lb_config, Regex::Engine& regex_engine,
                              absl::Status& creation_status);

  RendezvousHashLbProto lb_config_;
};

/**
 * Implement Rendezvous Hashing (also known as Highest Random Weight or HRW).
 *
 * Rendezvous hashing works by computing a score for each host for a given key, and selecting
 * the host with the highest score. The score is computed using the weighted rendezvous hashing
 * formula: -weight / ln(hash(key, host_hash))
 *
 * This provides:
 * - Consistent hashing with minimal disruption when hosts are added/removed
 * - Better key distribution than ring hash
 * - Lower memory usage than ring hash
 * - O(n) picking time instead of O(log n) for ring hash
 *
 */
class RendezvousHashLoadBalancer : public ThreadAwareLoadBalancerBase {
  // Allow test access to private nested class.
  friend class RendezvousHashMathTest;

public:
  RendezvousHashLoadBalancer(const PrioritySet& priority_set, ClusterLbStats& stats,
                             Stats::Scope& scope, Runtime::Loader& runtime,
                             Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
                             const RendezvousHashLbProto& config, HashPolicySharedPtr hash_policy);

private:
  /**
   * Internal representation of a host with its precomputed hash and weight.
   */
  struct HostHashEntry {
    HostHashEntry(const HostConstSharedPtr& host, uint64_t hash, double weight)
        : host_(host), hash_(hash), weight_(weight) {}

    HostConstSharedPtr host_;
    uint64_t hash_;
    double weight_;
  };

  /**
   * The Rendezvous Hash table implementation.
   */
  class RendezvousHashTable : public HashingLoadBalancer,
                              protected Logger::Loggable<Logger::Id::upstream> {
    // Allow test access to private methods.
    friend class RendezvousHashMathTest;

  public:
    RendezvousHashTable(const NormalizedHostWeightVector& normalized_host_weights,
                        bool use_hostname_for_hashing);

    // ThreadAwareLoadBalancerBase::HashingLoadBalancer
    HostSelectionResponse chooseHost(uint64_t hash, uint32_t attempt) const override;

  private:
    /**
     * Computes the rendezvous hash score for the given key and host.
     * Uses the formula from
     * https://www.snia.org/sites/default/files/SDC15_presentations/dist_sys/Jason_Resch_New_Consistent_Hashings_Rev.pdf
     * to maintain minimal disruption guarantee of rendezvous hashing.
     */
    static double computeScore(uint64_t key, uint64_t host_hash, double weight);

    /**
     * Xorshift* PRNG to mix the input bits.
     * Based on: https://en.wikipedia.org/wiki/Xorshift#xorshift*
     */
    static uint64_t xorshiftMult64(uint64_t x);

    /**
     * Normalizes a hash value to the range [0.0, 1.0).
     * Uses the lower 53 bits to fit within double precision.
     */
    static double normalizeHash(uint64_t hash);

    /**
     * Fast approximation of ln(x) using log2(x) * ln(2).
     */
    static double fastLog(double x);

    /**
     * Fast log2 approximation using bit manipulation.
     * Based on:
     * https://innovation.ebayinc.com/stories/fast-approximate-logarithms-part-iii-the-formulas/
     */
    static double fastLog2(double x);

    std::vector<HostHashEntry> hosts_;
  };

  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr
  createLoadBalancer(const NormalizedHostWeightVector& normalized_host_weights,
                     double min_normalized_weight, double max_normalized_weight) override;

  Stats::ScopeSharedPtr scope_;
  const bool use_hostname_for_hashing_;
  const uint32_t hash_balance_factor_;
};

} // namespace Upstream
} // namespace Envoy
