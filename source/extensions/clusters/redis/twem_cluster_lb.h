#pragma once

#include <array>
#include <string>
#include <vector>

#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "common/common/logger.h"
#include "common/network/address_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/upstream_impl.h"

#include "source/extensions/clusters/redis/twem_hash.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class TwemLoadBalancerContextImpl : public Upstream::LoadBalancerContextBase {
public:
  TwemLoadBalancerContextImpl(std::string key): hash_key_(TwemHash::fnv1a64(key)) {}
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }
private:
  const absl::optional<uint64_t> hash_key_;
};

class TwemClusterThreadAwareLoadBalancer: public Upstream::RingHashLoadBalancer {
public:
  using Upstream::RingHashLoadBalancer::RingHashLoadBalancer;

private:
  friend class TwemLoadBalancerRingTest;
  using HashFunction = envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction;

  struct Ring : public Upstream::RingHashLoadBalancer::Ring {
    using Upstream::RingHashLoadBalancer::Ring::Ring;
    void doHash(const std::string& address_string, HashFunction , uint64_t i, std::vector<uint64_t> &hashes) override;
  };

  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr
  createLoadBalancer(const Upstream::NormalizedHostWeightVector& normalized_host_weights,
                     double min_normalized_weight, double /* max_normalized_weight */) override {
    auto ring_size = normalized_host_weights.size() * 40;
    // TwemClusterThreadAwareLoadBalancer does not use the hash_function param.
    HashFunction null_hash;
    HashingLoadBalancerSharedPtr ring_hash_lb =
        std::make_shared<Ring>(normalized_host_weights, min_normalized_weight, ring_size,
                               ring_size, null_hash, use_hostname_for_hashing_, stats_);
    ring_hash_lb->init();
    return ring_hash_lb;
  }
};

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
