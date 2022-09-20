#pragma once

#include <cmath>
#include <random>

#include "envoy/common/random_generator.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/upstream/ring_hash_lb.h"
#include "source/common/upstream/upstream_impl.h"

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Upstream {

/**
 * Concepts:
 * `https://blog.twitter.com/engineering/en_us/topics/infrastructure/2019/daperture-load-balancer`
 *
 * Note: This implementation tries to closely match original:
 * `https://github.com/twitter/finagle/tree/finagle-19.6.0/finagle-core/src/main/scala/com/twitter/finagle/loadbalancer/aperture`.
 *
 * High level ideas:
 *      - Conserve connections made to the backend/endpoints by dividing the backends uniformly
 *      among participating peers. This way, we avoid a complete mesh of connections between Envoy
 *      and backends.
 * Implementation:
 *      - Uses a `Hash Ring` for the endpoints.
 *      - Envoy peers are placed on a ring that is conceptually laid on top of the Ring Hash of
 * backends to determine their overlaps.
 *      - By using the ring overlaps as a mechanism to divide the
 * backends, the backends get divided as a fraction of their overlaps with each peer.
 *      - The algorithm uses a uniform random distribution to select a backend from the range of
 * overlapping backends. This along with P2C ensures uniform load distribution.
 */

/**
 * All DeterministicAperture load balancer ring stats. @see stats_macros.h
 */
#define ALL_DETERMINISTIC_APERTURE_LOAD_BALANCER_RING_STATS(COUNTER) COUNTER(pick2_same)

/**
 * Struct definition for all DeterministicAperture load balancer ring stats. @see stats_macros.h
 */
struct DeterministicApertureLoadBalancerRingStats {
  ALL_DETERMINISTIC_APERTURE_LOAD_BALANCER_RING_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Thread aware load balancer implementation for DeterministicAperture.
 */
class DeterministicApertureLoadBalancer : public RingHashLoadBalancer {
public:
  DeterministicApertureLoadBalancer(
      const PrioritySet& priority_set, ClusterStats& stats, Stats::Scope& scope,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      const absl::optional<envoy::config::cluster::v3::Cluster::DeterministicApertureLbConfig>&
          config,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);

  using HashFunction = envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction;

  const RingHashLoadBalancerStats& ringStats() const { return ring_stats_; }
  /*
   * Customization of the `RingHashLoadBalancer` ring to add functionality that allows calculating
   * the intersecting ring segments.
   */
  struct Ring : public RingHashLoadBalancer::Ring {
    Ring(double offset, double width, const NormalizedHostWeightVector& normalized_host_weights,
         double min_normalized_weight, uint64_t min_ring_size, uint64_t max_ring_size,
         HashFunction hash_function, bool use_hostname_for_hashing, Stats::ScopeSharedPtr scope,
         RingHashLoadBalancerStats ring_stats);

    // ThreadAwareLoadBalancerBase::HashingLoadBalancer
    HostConstSharedPtr chooseHost(uint64_t hash, uint32_t attempt) const override;

    //
    // Utility Ring methods
    //

    /*
     * The ratio of intersection of the two rings.
     * @param index Index of the inner ring
     * @param offset Offset of the outer/peer ring
     * @param width Width for which the overlap has to be calculated.
     */
    absl::optional<double> weight(size_t index, double offset, double width) const;


    /*
     * Gets the index of the Ring's entry at a given offset.
     * @param offset for which index is to be calculated.
     */
    size_t getIndex(double offset) const;

    /*
     * Pick an index as part of the `p2c` algorithm.
     * Uses uniform random distribution within to pick a random index in the peer's `offset` and
     * `width`.
     */
    size_t pick() const;

    /*
     * Pick another index in the peer's `offset` and `width` range.
     * @param first Index that was already picked. The new pick cannot overlap the first pick's
     * region.
     */
    size_t tryPickSecond(size_t first) const;

    /*
     * Picks two indexes as per the `p2c` algorithm.
     */
    std::pair<size_t, size_t> pick2() const;

  private:
    static DeterministicApertureLoadBalancerRingStats generateStats(Stats::Scope& scope);

    const double offset_;
    const double width_;
    const double unit_width_;
    std::random_device random_dev_;
    mutable std::mt19937 rng_;
    mutable std::uniform_real_distribution<double> random_distribution_;
    DeterministicApertureLoadBalancerRingStats stats_;

    double intersect(double b0, double e0, double b1, double e1) const;
    double nextRandom() const { return random_distribution_(rng_); }
  };

  using RingConstSharedPtr = std::shared_ptr<const Ring>;

private:
  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr
  createLoadBalancer(const NormalizedHostWeightVector& normalized_host_weights,
                     double min_normalized_weight, double /* max_normalized_weight */) override {
    HashingLoadBalancerSharedPtr deterministic_aperture_lb = std::make_shared<Ring>(
        offset_, width_, normalized_host_weights, min_normalized_weight, min_ring_size_,
        max_ring_size_, hash_function_, use_hostname_for_hashing_, scope_, ring_stats_);
    if (hash_balance_factor_ == 0) {
      return deterministic_aperture_lb;
    }

    return std::make_shared<BoundedLoadHashingLoadBalancer>(
        deterministic_aperture_lb, std::move(normalized_host_weights), hash_balance_factor_);
  }

  double width_;
  double offset_;
  Stats::ScopeSharedPtr scope_;
  RingHashLoadBalancerStats ring_stats_;
};

} // namespace Upstream
} // namespace Envoy
