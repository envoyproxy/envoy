#include "redis_cluster_lb.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

// RedisClusterLoadBalancerFactory
bool RedisClusterLoadBalancerFactory::onClusterSlotUpdate(
    const std::vector<Envoy::Extensions::Clusters::Redis::ClusterSlot>& slots,
    Envoy::Upstream::HostMap all_hosts) {

  SlotArraySharedPtr current;
  {
    absl::ReaderMutexLock lock(&mutex_);
    current = slot_array_;
  }

  bool should_update = !current;
  auto updated_slots = std::make_shared<SlotArray>();
  for (const ClusterSlot& slot : slots) {
    auto host = all_hosts.find(slot.master()->asString());
    ASSERT(host != all_hosts.end(), "we expect all address to be found in the updated_hosts");
    for (auto i = slot.start(); i <= slot.end(); ++i) {
      updated_slots->at(i) = host->second;
      if (current && current->at(i)->address()->asString() != host->second->address()->asString()) {
        should_update = true;
      }
    }
  }

  if (should_update) {
    absl::WriterMutexLock lock(&mutex_);
    slot_array_ = updated_slots;
  }
  return should_update;
}

Upstream::LoadBalancerPtr RedisClusterLoadBalancerFactory::create() {
  absl::ReaderMutexLock lock(&mutex_);
  return std::make_unique<RedisClusterLoadBalancer>(slot_array_);
}

Upstream::HostConstSharedPtr
RedisClusterLoadBalancer::chooseHost(Envoy::Upstream::LoadBalancerContext* context) {
  if (!slot_array_) {
    return nullptr;
  }
  absl::optional<uint64_t> hash;
  if (context) {
    hash = context->computeHashKey();
  }

  if (!hash) {
    return nullptr;
  }

  return slot_array_->at(hash.value() % Envoy::Extensions::Clusters::Redis::MaxSlot);
}

RedisLoadBalancerContext::RedisLoadBalancerContext(const std::string& key, bool enabled_hashtagging,
                                                   bool use_crc16)
    : hash_key_(use_crc16 ? Crc16::crc16(hashtag(key, enabled_hashtagging))
                          : MurmurHash::murmurHash2_64(hashtag(key, enabled_hashtagging))) {}

// Inspired by the redis-cluster hashtagging algorithm
// https://redis.io/topics/cluster-spec#keys-hash-tags
absl::string_view RedisLoadBalancerContext::hashtag(absl::string_view v, bool enabled) {
  if (!enabled) {
    return v;
  }

  auto start = v.find('{');
  if (start == std::string::npos) {
    return v;
  }

  auto end = v.find('}', start);
  if (end == std::string::npos || end == start + 1) {
    return v;
  }

  return v.substr(start + 1, end - start - 1);
}
} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
