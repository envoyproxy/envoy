#include "common/upstream/ring_hash_lb.h"

#include <cstdint>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/upstream/load_balancer_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

RingHashLoadBalancer::RingHashLoadBalancer(
    PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random,
    const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config)
    : LoadBalancerBase(priority_set, stats, runtime, random), config_(config),
      factory_(new LoadBalancerFactoryImpl(stats, random)) {}

void RingHashLoadBalancer::initialize() {
  // TODO(mattklein123): In the future, once initialized and the initial ring is built, it would be
  // better to use a background thread for computing ring updates. This has the substantial benefit
  // that if the ring computation thread falls behind, host set updates can be trivially collapsed.
  // I will look into doing this in a follow up. Doing everything using a background thread heavily
  // complicated initialization as the load balancer would need its own initialized callback. I
  // think the synchronous/asynchronous split is probably the best option.
  priority_set_.addMemberUpdateCb([this](uint32_t, const std::vector<HostSharedPtr>&,
                                         const std::vector<HostSharedPtr>&) -> void { refresh(); });

  refresh();
}

HostConstSharedPtr
RingHashLoadBalancer::LoadBalancerImpl::chooseHost(LoadBalancerContext* context) {
  // Make sure we correctly return nullptr for any early chooseHost() calls.
  if (per_priority_state_ == nullptr) {
    return nullptr;
  }
  // If there is no hash in the context, just choose a random value (this effectively becomes
  // the random LB but it won't crash if someone configures it this way).
  // computeHashKey() may be computed on demand, so get it only once.
  Optional<uint64_t> hash;
  if (context) {
    hash = context->computeHashKey();
  }
  const uint64_t h = hash.valid() ? hash.value() : random_.random();

  const uint32_t priority = LoadBalancerBase::choosePriority(h, *per_priority_load_);
  if ((*per_priority_state_)[priority]->global_panic_) {
    stats_.lb_healthy_panic_.inc();
  }
  return (*per_priority_state_)[priority]->current_ring_->chooseHost(h);
}

LoadBalancerPtr RingHashLoadBalancer::LoadBalancerFactoryImpl::create() {
  auto lb = std::make_unique<LoadBalancerImpl>(stats_, random_);

  // We must protect current_ring_ via a RW lock since it is accessed and written to by multiple
  // threads. All complex processing has already been precalculated however.
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);
  lb->per_priority_load_ = per_priority_load_;
  lb->per_priority_state_ = per_priority_state_;

  return std::move(lb);
}

HostConstSharedPtr RingHashLoadBalancer::Ring::chooseHost(uint64_t h) const {
  if (ring_.empty()) {
    return nullptr;
  }

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

RingHashLoadBalancer::Ring::Ring(const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
                                 const std::vector<HostSharedPtr>& hosts) {
  ENVOY_LOG(trace, "ring hash: building ring");
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

  char hash_key_buffer[196];
  for (const auto& host : hosts) {
    const std::string& address_string = host->address()->asString();
    uint64_t offset_start = address_string.size();

    // Currently, we support both IP and UDS addresses. The UDS max path length is ~108 on all Unix
    // platforms that I know of. Given that, we can use a 196 char buffer which is plenty of room
    // for UDS, '_', and up to 21 characters for the node ID. To be on the super safe side, there
    // is a RELEASE_ASSERT here that checks this, in case someone in the future adds some type of
    // new address that is larger, or runs on a platform where UDS is larger. I don't think it's
    // worth the defensive coding to deal with the heap allocation case (e.g. via
    // absl::InlinedVector) at the current time.
    RELEASE_ASSERT(address_string.size() + 1 + StringUtil::MIN_ITOA_OUT_LEN <=
                   sizeof(hash_key_buffer));
    memcpy(hash_key_buffer, address_string.c_str(), offset_start);
    hash_key_buffer[offset_start++] = '_';
    for (uint64_t i = 0; i < hashes_per_host; i++) {
      const uint64_t total_hash_key_len =
          offset_start +
          StringUtil::itoa(hash_key_buffer + offset_start, StringUtil::MIN_ITOA_OUT_LEN, i);
      absl::string_view hash_key(hash_key_buffer, total_hash_key_len);

      // Sadly std::hash provides no mechanism for hashing arbitrary bytes so we must copy here.
      // xxHash is done wihout copies.
      const uint64_t hash = use_std_hash ? std::hash<std::string>()(std::string(hash_key))
                                         : HashUtil::xxHash64(hash_key);
      ENVOY_LOG(trace, "ring hash: hash_key={} hash={}", hash_key.data(), hash);
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

void RingHashLoadBalancer::refresh() {
  auto per_priority_state = std::make_shared<std::vector<PerPriorityStatePtr>>(
      priority_set_.hostSetsPerPriority().size());
  auto per_priority_load = std::make_shared<std::vector<uint32_t>>(per_priority_load_);

  // Note that we only compute global panic on host set refresh. Given that the runtime setting will
  // rarely change, this is a reasonable compromise to avoid creating extra rings when we only
  // need to create one per priority level.
  for (auto& host_set : priority_set_.hostSetsPerPriority()) {
    uint32_t priority = host_set->priority();
    (*per_priority_state)[priority].reset(new PerPriorityState);
    if (isGlobalPanic(*host_set, runtime_)) {
      (*per_priority_state)[priority]->current_ring_ =
          std::make_shared<Ring>(config_, host_set->hosts());
      (*per_priority_state)[priority]->global_panic_ = true;
    } else {
      (*per_priority_state)[priority]->current_ring_ =
          std::make_shared<Ring>(config_, host_set->healthyHosts());
      (*per_priority_state)[priority]->global_panic_ = false;
    }
  }

  {
    std::unique_lock<std::shared_timed_mutex> lock(factory_->mutex_);
    factory_->per_priority_load_ = per_priority_load;
    factory_->per_priority_state_ = per_priority_state;
  }
}

} // namespace Upstream
} // namespace Envoy
