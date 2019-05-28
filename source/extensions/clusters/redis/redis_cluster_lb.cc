#include "redis_cluster_lb.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

// RedisClusterLoadBalancerFactory
void RedisClusterLoadBalancerFactory::onClusterSlotUpdate(
    const std::vector<Envoy::Extensions::Clusters::Redis::ClusterSlot>& slots,
    Envoy::Upstream::HostMap all_hosts) {

  auto slots_array = std::make_shared<SlotArray>();
  for (const ClusterSlot& slot : slots) {
    auto host = all_hosts.find(slot.master()->asString());
    ASSERT(host != all_hosts.end(), "we expect all address to be found in the updated_hosts");
    for (auto i = slot.start(); i <= slot.end(); ++i) {
      slots_array->at(i) = host->second;
    }
  }
  {
    absl::WriterMutexLock lock(&mutex_);
    slot_array_ = slots_array;
  }
}

Upstream::LoadBalancerPtr RedisClusterLoadBalancerFactory::create() {
  absl::ReaderMutexLock lock(&mutex_);
  return std::make_unique<RedisClusterLoadBalancer>(slot_array_);
}

Upstream::HostConstSharedPtr
RedisClusterLoadBalancer::chooseHost(Envoy::Upstream::LoadBalancerContext* context) {
  absl::optional<uint64_t> hash;
  if (context) {
    hash = context->computeHashKey();
  }

  if (!hash) {
    return nullptr;
  }

  uint64_t slot = hash.value() % Envoy::Extensions::Clusters::Redis::MAX_SLOT;
  return slot_array_->at(slot);
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
