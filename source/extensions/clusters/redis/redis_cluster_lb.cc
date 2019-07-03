#include "redis_cluster_lb.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

bool ClusterSlot::operator==(const Envoy::Extensions::Clusters::Redis::ClusterSlot& rhs) const {
  return start_ == rhs.start_ && end_ == rhs.end_ && master_ == rhs.master_ &&
         slaves_ == rhs.slaves_;
}

// RedisClusterLoadBalancerFactory
bool RedisClusterLoadBalancerFactory::onClusterSlotUpdate(ClusterSlotsPtr&& slots,
                                                          Envoy::Upstream::HostMap all_hosts) {

  // The slots is sorted, allowing for a quick comparison to make sure we need to update the slot
  // array sort based on start and end to enable efficient comparison
  sort(slots->begin(), slots->end(), [](const ClusterSlot& lhs, const ClusterSlot& rhs) -> bool {
    return lhs.start() < rhs.start() || (!(lhs.start() < rhs.start()) && lhs.end() < rhs.end());
  });

  if (current_cluster_slot_ && *current_cluster_slot_ == *slots) {
    return false;
  }

  auto updated_slots = std::make_shared<SlotArray>();
  ShardMap shards;

  for (const ClusterSlot& slot : *slots) {
    // look in the updated map
    const std::string master_address = slot.master()->asString();

    auto result = shards.try_emplace(master_address, nullptr);
    if (result.second) {
      auto master_host = all_hosts.find(master_address);
      ASSERT(master_host != all_hosts.end(),
             "we expect all address to be found in the updated_hosts");
      result.first->second = std::make_shared<RedisShard>();
      result.first->second->master_ = master_host->second;

      Upstream::HostVectorSharedPtr master_and_replicas = std::make_shared<Upstream::HostVector>();
      Upstream::HostVectorSharedPtr replicas = std::make_shared<Upstream::HostVector>();
      master_and_replicas->push_back(master_host->second);

      for (auto slave : slot.slaves()) {
        auto slave_host = all_hosts.find(slave->asString());
        ASSERT(slave_host != all_hosts.end(),
               "we expect all address to be found in the updated_hosts");
        replicas->push_back(slave_host->second);
        master_and_replicas->push_back(slave_host->second);
      }

      result.first->second->replicas_.updateHosts(
          Upstream::HostSetImpl::partitionHosts(replicas, Upstream::HostsPerLocalityImpl::empty()),
          nullptr, {}, {});
      result.first->second->all_hosts_.updateHosts(
          Upstream::HostSetImpl::partitionHosts(master_and_replicas,
                                                Upstream::HostsPerLocalityImpl::empty()),
          nullptr, {}, {});
    }

    for (auto i = slot.start(); i <= slot.end(); ++i) {
      updated_slots->at(i) = result.first->second;
    }
  }

  {
    absl::WriterMutexLock lock(&mutex_);
    current_cluster_slot_ = std::move(slots);
    slot_array_ = std::move(updated_slots);
  }
  return true;
}

Upstream::LoadBalancerPtr RedisClusterLoadBalancerFactory::create() {
  absl::ReaderMutexLock lock(&mutex_);
  return std::make_unique<RedisClusterLoadBalancer>(slot_array_, random_);
}

namespace {
Upstream::HostConstSharedPtr chooseRandomHost(Upstream::HostSetImpl& host_set,
                                              Runtime::RandomGenerator& random) {
  auto hosts = host_set.healthyHosts();
  if (hosts.empty()) {
    hosts = host_set.degradedHosts();
  }

  if (hosts.empty()) {
    hosts = host_set.hosts();
  }

  if (!hosts.empty()) {
    return hosts[random.random() % hosts.size()];
  } else {
    return nullptr;
  }
}
} // namespace

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

  auto shard = slot_array_->at(hash.value() % Envoy::Extensions::Clusters::Redis::MaxSlot);

  auto redis_context = dynamic_cast<RedisLoadBalancerContext*>(context);
  if (redis_context && redis_context->isReadCommand()) {
    switch (redis_context->readPolicy()) {
    case NetworkFilters::Common::Redis::Client::ReadPolicy::Master:
      return shard->master_;
    case NetworkFilters::Common::Redis::Client::ReadPolicy::PreferMaster:
      if (shard->master_->health() == Upstream::Host::Health::Healthy) {
        return shard->master_;
      } else {
        return chooseRandomHost(shard->all_hosts_, random_);
      }
    case NetworkFilters::Common::Redis::Client::ReadPolicy::Replica:
      return chooseRandomHost(shard->replicas_, random_);
    case NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica:
      if (!shard->replicas_.healthyHosts().empty()) {
        return chooseRandomHost(shard->replicas_, random_);
      } else {
        return chooseRandomHost(shard->all_hosts_, random_);
      }
    case NetworkFilters::Common::Redis::Client::ReadPolicy::Any:
      return chooseRandomHost(shard->all_hosts_, random_);
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  return shard->master_;
}

namespace {
bool isReadRequest(const NetworkFilters::Common::Redis::RespValue& request) {
  if (request.type() != NetworkFilters::Common::Redis::RespType::Array) {
    return false;
  }
  auto first = request.asArray()[0];
  if (first.type() != NetworkFilters::Common::Redis::RespType::SimpleString &&
      first.type() != NetworkFilters::Common::Redis::RespType::BulkString) {
    return false;
  }
  return NetworkFilters::Common::Redis::SupportedCommands::isReadCommand(first.asString());
}
} // namespace

RedisLoadBalancerContextImpl::RedisLoadBalancerContextImpl(
    const std::string& key, bool enabled_hashtagging, bool use_crc16,
    const NetworkFilters::Common::Redis::RespValue& request,
    NetworkFilters::Common::Redis::Client::ReadPolicy read_policy)
    : hash_key_(use_crc16 ? Crc16::crc16(hashtag(key, enabled_hashtagging))
                          : MurmurHash::murmurHash2_64(hashtag(key, enabled_hashtagging))),
      is_read_(isReadRequest(request)), read_policy_(read_policy) {}

// Inspired by the redis-cluster hashtagging algorithm
// https://redis.io/topics/cluster-spec#keys-hash-tags
absl::string_view RedisLoadBalancerContextImpl::hashtag(absl::string_view v, bool enabled) {
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
