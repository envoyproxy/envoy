#pragma once

#include <vector>

#include "source/common/common/logger.h"
#include "source/common/upstream/thread_aware_lb_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * All ring stats. @see stats_macros.h
 */
#define ALL_RING_STATS(GAUGE)                                                                      \
  GAUGE(max_hashes_per_host, Accumulate)                                                           \
  GAUGE(min_hashes_per_host, Accumulate)                                                           \
  GAUGE(size, Accumulate)

/**
 * Struct definition for all ring stats. @see stats_macros.h
 */
struct RingStats {
  ALL_RING_STATS(GENERATE_GAUGE_STRUCT)
};

// Provides base implementation of Ring based `ThreadAwareLoadBalancerBase::HashingLoadBalancer`.
class Ring : public ThreadAwareLoadBalancerBase::HashingLoadBalancer,
             public Logger::Loggable<Logger::Id::upstream> {
public:
  static const uint64_t DefaultMinRingSize = 1024;
  static const uint64_t DefaultMaxRingSize = 1024 * 1024 * 8;

  enum class HashFunction {
    // Use `xxHash <https://github.com/Cyan4973/xxHash>`_, this is the default hash function.
    XX_HASH,

    // Use `MurmurHash2 <https://sites.google.com/site/murmurhash/>`_, this is compatible with
    // std:hash<string> in GNU libstdc++ 3.4.20 or above. This is typically the case when compiled
    // on Linux and not macOS.
    MURMUR_HASH_2
  };

  struct Entry {
    uint64_t hash_;
    HostConstSharedPtr host_;
  };

  Ring(const NormalizedHostWeightVector& normalized_host_weights, double min_normalized_weight,
       uint64_t min_ring_size, uint64_t max_ring_size, HashFunction hash_function,
       bool use_hostname_for_hashing, RingStats& stats);

  // ThreadAwareLoadBalancerBase::HashingLoadBalancer
  HostConstSharedPtr chooseHost(uint64_t hash, uint32_t attempt) const override;

  static RingStats generateStats(Stats::Scope& scope);

protected:
  std::vector<Entry> ring_;
  uint64_t ring_size_{0};
};

} // namespace Upstream
} // namespace Envoy
