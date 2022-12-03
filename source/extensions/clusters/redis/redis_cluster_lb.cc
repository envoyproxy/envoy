#include "redis_cluster_lb.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

bool ClusterSlot::operator==(const Envoy::Extensions::Clusters::Redis::ClusterSlot& rhs) const {
  if (start_ != rhs.start_ || end_ != rhs.end_ || *primary_ != *rhs.primary_ ||
      replicas_.size() != rhs.replicas_.size()) {
    return false;
  }
  // The value type is shared_ptr, and the shared_ptr is not same one even for same ip:port.
  // so, just compare the key here.
  return std::equal(replicas_.begin(), replicas_.end(), rhs.replicas_.begin(), rhs.replicas_.end(),
                    [](const auto& it1, const auto& it2) { return it1.first == it2.first; });
}

// RedisClusterLoadBalancerFactory
bool RedisClusterLoadBalancerFactory::onClusterSlotUpdate(ClusterSlotsSharedPtr&& slots,
                                                          Envoy::Upstream::HostMap& all_hosts) {
  // The slots is sorted, allowing for a quick comparison to make sure we need to update the slot
  // array sort based on start and end to enable efficient comparison
  std::sort(
      slots->begin(), slots->end(), [](const ClusterSlot& lhs, const ClusterSlot& rhs) -> bool {
        return lhs.start() < rhs.start() || (!(lhs.start() < rhs.start()) && lhs.end() < rhs.end());
      });

  if (current_cluster_slot_ && *current_cluster_slot_ == *slots) {
    return false;
  }

  auto updated_slots = std::make_shared<SlotArray>();
  auto shard_vector = std::make_shared<std::vector<RedisShardSharedPtr>>();
  absl::flat_hash_map<std::string, uint64_t> shards;

  for (const ClusterSlot& slot : *slots) {
    // look in the updated map
    const std::string primary_address = slot.primary()->asString();

    auto result = shards.try_emplace(primary_address, shard_vector->size());
    if (result.second) {
      auto primary_host = all_hosts.find(primary_address);
      ASSERT(primary_host != all_hosts.end(),
             "we expect all address to be found in the updated_hosts");

      Upstream::HostVectorSharedPtr primary_and_replicas = std::make_shared<Upstream::HostVector>();
      Upstream::HostVectorSharedPtr replicas = std::make_shared<Upstream::HostVector>();
      primary_and_replicas->push_back(primary_host->second);

      for (auto const& replica : slot.replicas()) {
        auto replica_host = all_hosts.find(replica.first);
        ASSERT(replica_host != all_hosts.end(),
               "we expect all address to be found in the updated_hosts");
        replicas->push_back(replica_host->second);
        primary_and_replicas->push_back(replica_host->second);
      }

      shard_vector->emplace_back(
          std::make_shared<RedisShard>(primary_host->second, replicas, primary_and_replicas));
    }

    for (auto i = slot.start(); i <= slot.end(); ++i) {
      updated_slots->at(i) = result.first->second;
    }
  }

  {
    absl::WriterMutexLock lock(&mutex_);
    current_cluster_slot_ = std::move(slots);
    slot_array_ = std::move(updated_slots);
    shard_vector_ = std::move(shard_vector);
  }
  return true;
}

void RedisClusterLoadBalancerFactory::onHostHealthUpdate() {
  ShardVectorSharedPtr current_shard_vector;
  {
    absl::ReaderMutexLock lock(&mutex_);
    current_shard_vector = shard_vector_;
  }

  // This can get called by cluster initialization before the Redis Cluster topology is resolved.
  if (!current_shard_vector) {
    return;
  }

  auto shard_vector = std::make_shared<std::vector<RedisShardSharedPtr>>();

  for (auto const& shard : *current_shard_vector) {
    shard_vector->emplace_back(std::make_shared<RedisShard>(
        shard->primary(), shard->replicas().hostsPtr(), shard->allHosts().hostsPtr()));
  }

  {
    absl::WriterMutexLock lock(&mutex_);
    shard_vector_ = std::move(shard_vector);
  }
}

Upstream::LoadBalancerPtr RedisClusterLoadBalancerFactory::create() {
  absl::ReaderMutexLock lock(&mutex_);
  return std::make_unique<RedisClusterLoadBalancer>(slot_array_, shard_vector_, random_);
}

namespace {
Upstream::HostConstSharedPtr chooseRandomHost(const Upstream::HostSetImpl& host_set,
                                              Random::RandomGenerator& random) {
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

Upstream::HostConstSharedPtr RedisClusterLoadBalancerFactory::RedisClusterLoadBalancer::chooseHost(
    Envoy::Upstream::LoadBalancerContext* context) {
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

  auto shard = shard_vector_->at(
      slot_array_->at(hash.value() % Envoy::Extensions::Clusters::Redis::MaxSlot));

  auto redis_context = dynamic_cast<RedisLoadBalancerContext*>(context);
  if (redis_context && redis_context->isReadCommand()) {
    switch (redis_context->readPolicy()) {
    case NetworkFilters::Common::Redis::Client::ReadPolicy::Primary:
      return shard->primary();
    case NetworkFilters::Common::Redis::Client::ReadPolicy::PreferPrimary:
      if (shard->primary()->coarseHealth() == Upstream::Host::Health::Healthy) {
        return shard->primary();
      } else {
        return chooseRandomHost(shard->allHosts(), random_);
      }
    case NetworkFilters::Common::Redis::Client::ReadPolicy::Replica:
      return chooseRandomHost(shard->replicas(), random_);
    case NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica:
      if (!shard->replicas().healthyHosts().empty()) {
        return chooseRandomHost(shard->replicas(), random_);
      } else {
        return chooseRandomHost(shard->allHosts(), random_);
      }
    case NetworkFilters::Common::Redis::Client::ReadPolicy::Any:
      return chooseRandomHost(shard->allHosts(), random_);
    }
  }
  return shard->primary();
}

bool RedisLoadBalancerContextImpl::isReadRequest(
    const NetworkFilters::Common::Redis::RespValue& request) {
  const NetworkFilters::Common::Redis::RespValue* command = nullptr;
  if (request.type() == NetworkFilters::Common::Redis::RespType::Array) {
    command = &(request.asArray()[0]);
  } else if (request.type() == NetworkFilters::Common::Redis::RespType::CompositeArray) {
    command = request.asCompositeArray().command();
  }
  if (!command) {
    return false;
  }
  if (command->type() != NetworkFilters::Common::Redis::RespType::SimpleString &&
      command->type() != NetworkFilters::Common::Redis::RespType::BulkString) {
    return false;
  }
  std::string to_lower_string = absl::AsciiStrToLower(command->asString());
  return NetworkFilters::Common::Redis::SupportedCommands::isReadCommand(to_lower_string);
}

RedisLoadBalancerContextImpl::RedisLoadBalancerContextImpl(
    const std::string& key, bool enabled_hashtagging, bool is_redis_cluster,
    const NetworkFilters::Common::Redis::RespValue& request,
    NetworkFilters::Common::Redis::Client::ReadPolicy read_policy)
    : hash_key_(is_redis_cluster ? Crc16::crc16(hashtag(key, true))
                                 : MurmurHash::murmurHash2(hashtag(key, enabled_hashtagging))),
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
