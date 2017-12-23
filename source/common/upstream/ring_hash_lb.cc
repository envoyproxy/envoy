#include "common/upstream/ring_hash_lb.h"

#include <cstdint>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Upstream {

RingHashLoadBalancer::RingHashLoadBalancer(
    PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random,
    const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config)
    : LoadBalancerBase(priority_set, stats, runtime, random), config_(config) {
  priority_set.addMemberUpdateCb(
      [this](uint32_t priority, const std::vector<HostSharedPtr>&,
             const std::vector<HostSharedPtr>&) -> void { refresh(priority); });

  for (auto& host_set : priority_set_.hostSetsPerPriority()) {
    refresh(host_set->priority());
  }
}

HostConstSharedPtr RingHashLoadBalancer::chooseHost(LoadBalancerContext* context) {
  const HostSet& host_set = chooseHostSet();
  if (isGlobalPanic(host_set, runtime_)) {
    stats_.lb_healthy_panic_.inc();
    return per_priority_state_[host_set.priority()]->all_hosts_ring_.chooseHost(context, random_);
  } else {
    return per_priority_state_[host_set.priority()]->healthy_hosts_ring_.chooseHost(context,
                                                                                    random_);
  }
}

HostConstSharedPtr RingHashLoadBalancer::Ring::chooseHost(LoadBalancerContext* context,
                                                          Runtime::RandomGenerator& random) {
  if (ring_.empty()) {
    return nullptr;
  }

  // If there is no hash in the context, just choose a random value (this effectively becomes
  // the random LB but it won't crash if someone configures it this way).
  // computeHashKey() may be computed on demand, so get it only once.
  Optional<uint64_t> hash;
  if (context) {
    hash = context->computeHashKey();
  }
  const uint64_t h = hash.valid() ? hash.value() : random.random();

  // Ported from https://github.com/RJ/ketama/blob/master/libketama/ketama.c (ketama_get_server)
  // I've generally kept the variable names to make the code easier to compare.
  // NOTE: The algorithm depends on using signed integers for lowp, midp, and highp. Do not
  //       change them!
  int64_t lowp = 0;
  int64_t highp = ring_.size();
  while (true) {
    int64_t midp = (lowp + highp) / 2;

    if (midp == static_cast<int64_t>(ring_.size())) {
      return ring_[0].host_;
    }

    uint64_t midval = ring_[midp].hash_;
    uint64_t midval1 = midp == 0 ? 0 : ring_[midp - 1].hash_;

    if (h <= midval && h > midval1) {
      return ring_[midp].host_;
    }

    if (midval < h) {
      lowp = midp + 1;
    } else {
      highp = midp - 1;
    }

    if (lowp > highp) {
      return ring_[0].host_;
    }
  }
}

void RingHashLoadBalancer::Ring::create(
    const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
    const std::vector<HostSharedPtr>& hosts) {
  ENVOY_LOG(trace, "ring hash: building ring");
  ring_.clear();
  if (hosts.empty()) {
    return;
  }

  // Currently we specify the minimum size of the ring, and determine the replication factor
  // based on the number of hosts. It's possible we might want to support more sophisticated
  // configuration in the future.
  // NOTE: Currently we keep a ring for healthy hosts and unhealthy hosts, and this is done per
  //       thread. This is the simplest implementation, but it's expensive from a memory
  //       standpoint and duplicates the regeneration computation. In the future we might want
  //       to generate the rings centrally and then just RCU them out to each thread. This is
  //       sufficient for getting started.
  const uint64_t min_ring_size =
      config.valid() ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), minimum_ring_size, 1024)
                     : 1024;

  uint64_t hashes_per_host = 1;
  if (hosts.size() < min_ring_size) {
    hashes_per_host = min_ring_size / hosts.size();
    if ((min_ring_size % hosts.size()) != 0) {
      hashes_per_host++;
    }
  }

  ENVOY_LOG(info, "ring hash: min_ring_size={} hashes_per_host={}", min_ring_size, hashes_per_host);
  ring_.reserve(hosts.size() * hashes_per_host);

  const bool use_std_hash =
      config.valid()
          ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value().deprecated_v1(), use_std_hash, true)
          : true;
  for (const auto& host : hosts) {
    for (uint64_t i = 0; i < hashes_per_host; i++) {
      const std::string hash_key(host->address()->asString() + "_" + std::to_string(i));
      const uint64_t hash =
          use_std_hash ? std::hash<std::string>()(hash_key) : HashUtil::xxHash64(hash_key);
      ENVOY_LOG(trace, "ring hash: hash_key={} hash={}", hash_key, hash);
      ring_.push_back({hash, host});
    }
  }

  std::sort(ring_.begin(), ring_.end(), [](const RingEntry& lhs, const RingEntry& rhs) -> bool {
    return lhs.hash_ < rhs.hash_;
  });
#ifndef NVLOG
  for (auto entry : ring_) {
    ENVOY_LOG(trace, "ring hash: host={} hash={}", entry.host_->address()->asString(), entry.hash_);
  }
#endif
}

void RingHashLoadBalancer::refresh(uint32_t priority) {
  while (per_priority_state_.size() < priority + 1) {
    per_priority_state_.push_back(PerPriorityStatePtr{new PerPriorityState});
  }
  auto& host_set = priority_set_.hostSetsPerPriority()[priority];
  per_priority_state_[priority]->all_hosts_ring_.create(config_, host_set->hosts());
  per_priority_state_[priority]->healthy_hosts_ring_.create(config_, host_set->healthyHosts());
}

} // namespace Upstream
} // namespace Envoy
