#pragma once

#include <array>
#include <string>
#include <vector>

#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/network/address_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/redis/crc16.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/codec.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"

#include "absl/container/btree_map.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

static const uint64_t MaxSlot = 16384;

using ReplicaToResolve = std::pair<std::string, uint16_t>;

class ClusterSlot {
public:
  ClusterSlot(int64_t start, int64_t end, Network::Address::InstanceConstSharedPtr primary)
      : start_(start), end_(end), primary_(std::move(primary)) {}

  int64_t start() const { return start_; }
  int64_t end() const { return end_; }
  Network::Address::InstanceConstSharedPtr primary() const { return primary_; }
  const absl::btree_map<std::string, Network::Address::InstanceConstSharedPtr>& replicas() const {
    return replicas_;
  }

  void setPrimary(Network::Address::InstanceConstSharedPtr address) {
    primary_ = std::move(address);
  }
  void addReplica(Network::Address::InstanceConstSharedPtr replica_address) {
    replicas_.emplace(replica_address->asString(), std::move(replica_address));
  }
  void addReplicaToResolve(const std::string& host, uint16_t port) {
    replicas_to_resolve_.emplace_back(host, port);
  }

  bool operator==(const ClusterSlot& rhs) const;

  // In case of primary slot address is hostname and needs to be resolved
  std::string primary_hostname_;
  uint16_t primary_port_;
  std::vector<ReplicaToResolve> replicas_to_resolve_;

private:
  int64_t start_;
  int64_t end_;
  Network::Address::InstanceConstSharedPtr primary_;
  absl::btree_map<std::string, Network::Address::InstanceConstSharedPtr> replicas_;
};

using ClusterSlotsPtr = std::unique_ptr<std::vector<ClusterSlot>>;
using ClusterSlotsSharedPtr = std::shared_ptr<std::vector<ClusterSlot>>;

class RedisLoadBalancerContext {
public:
  virtual ~RedisLoadBalancerContext() = default;

  virtual bool isReadCommand() const PURE;
  virtual NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const PURE;
};

class RedisLoadBalancerContextImpl : public RedisLoadBalancerContext,
                                     public Upstream::LoadBalancerContextBase {
public:
  /**
   * The load balancer context for Redis requests. Note that is_redis_cluster implies using Redis
   * cluster which require us to always enable hashtagging.
   * @param key specify the key for the Redis request.
   * @param enabled_hashtagging specify whether to enable hashtagging, this will always be true if
   * is_redis_cluster is true.
   * @param is_redis_cluster specify whether this is a request for redis cluster, if true the key
   * will be hashed using crc16.
   * @param request specify the Redis request.
   * @param read_policy specify the read policy.
   */
  RedisLoadBalancerContextImpl(const std::string& key, bool enabled_hashtagging,
                               bool is_redis_cluster,
                               const NetworkFilters::Common::Redis::RespValue& request,
                               NetworkFilters::Common::Redis::Client::ReadPolicy read_policy =
                                   NetworkFilters::Common::Redis::Client::ReadPolicy::Primary);

  // Upstream::LoadBalancerContextBase
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }

  bool isReadCommand() const override { return is_read_; }

  NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const override {
    return read_policy_;
  }

private:
  absl::string_view hashtag(absl::string_view v, bool enabled);

  static bool isReadRequest(const NetworkFilters::Common::Redis::RespValue& request);

  const absl::optional<uint64_t> hash_key_;
  const bool is_read_;
  const NetworkFilters::Common::Redis::Client::ReadPolicy read_policy_;
};

class ClusterSlotUpdateCallBack {
public:
  virtual ~ClusterSlotUpdateCallBack() = default;

  /**
   * Callback when cluster slot is updated
   * @param slots provides the updated cluster slots.
   * @param all_hosts provides the updated hosts.
   * @return indicate if the cluster slot is updated or not.
   */
  virtual bool onClusterSlotUpdate(ClusterSlotsSharedPtr&& slots,
                                   Upstream::HostMap& all_hosts) PURE;

  /**
   * Callback when a host's health status is updated
   */
  virtual void onHostHealthUpdate() PURE;
};

using ClusterSlotUpdateCallBackSharedPtr = std::shared_ptr<ClusterSlotUpdateCallBack>;

/**
 * This factory is created and returned by RedisCluster's factory() method, the create() method will
 * be called on each thread to create a thread local RedisClusterLoadBalancer.
 */
class RedisClusterLoadBalancerFactory : public ClusterSlotUpdateCallBack,
                                        public Upstream::LoadBalancerFactory {
public:
  RedisClusterLoadBalancerFactory(Random::RandomGenerator& random) : random_(random) {}

  // ClusterSlotUpdateCallBack
  bool onClusterSlotUpdate(ClusterSlotsSharedPtr&& slots, Upstream::HostMap& all_hosts) override;

  void onHostHealthUpdate() override;

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override;

private:
  class RedisShard {
  public:
    RedisShard(Upstream::HostConstSharedPtr primary, Upstream::HostVectorConstSharedPtr replicas,
               Upstream::HostVectorConstSharedPtr all_hosts, Random::RandomGenerator& random)
        : primary_(std::move(primary)) {
      replicas_.updateHosts(Upstream::HostSetImpl::partitionHosts(
                                std::move(replicas), Upstream::HostsPerLocalityImpl::empty()),
                            nullptr, {}, {}, random.random());
      all_hosts_.updateHosts(Upstream::HostSetImpl::partitionHosts(
                                 std::move(all_hosts), Upstream::HostsPerLocalityImpl::empty()),
                             nullptr, {}, {}, random.random());
    }
    const Upstream::HostConstSharedPtr primary() const { return primary_; }
    const Upstream::HostSetImpl& replicas() const { return replicas_; }
    const Upstream::HostSetImpl& allHosts() const { return all_hosts_; }

  private:
    const Upstream::HostConstSharedPtr primary_;
    Upstream::HostSetImpl replicas_{0, absl::nullopt, absl::nullopt};
    Upstream::HostSetImpl all_hosts_{0, absl::nullopt, absl::nullopt};
  };

  using RedisShardSharedPtr = std::shared_ptr<const RedisShard>;
  using ShardVectorSharedPtr = std::shared_ptr<std::vector<RedisShardSharedPtr>>;
  using SlotArray = std::array<uint64_t, MaxSlot>;
  using SlotArraySharedPtr = std::shared_ptr<const SlotArray>;

  /*
   * This class implements load balancing according to `Redis Cluster
   * <https://redis.io/topics/cluster-spec>`_. This load balancer is thread local and created
   * through the RedisClusterLoadBalancerFactory by the cluster manager.
   *
   * The topology is stored in slot_array_ and shard_vector_. According to the
   * `Redis Cluster Spec <https://redis.io/topics/cluster-spec#keys-distribution-model`_, the key
   * space is split into a fixed size 16384 slots. The current implementation uses a fixed size
   * std::array() of the index of the shard in the shard_vector_. This has a fixed cpu and memory
   * cost and provide a fast lookup constant time lookup similar to Maglev. This will be used by the
   * redis proxy filter for load balancing purpose.
   */
  class RedisClusterLoadBalancer : public Upstream::LoadBalancer {
  public:
    RedisClusterLoadBalancer(SlotArraySharedPtr slot_array, ShardVectorSharedPtr shard_vector,
                             Random::RandomGenerator& random)
        : slot_array_(std::move(slot_array)), shard_vector_(std::move(shard_vector)),
          random_(random) {}

    // Upstream::LoadBalancerBase
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext*) override;
    Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
      return nullptr;
    }
    // Pool selection not implemented.
    absl::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                             const Upstream::Host& /*host*/,
                             std::vector<uint8_t>& /*hash_key*/) override {
      return absl::nullopt;
    }
    // Lifetime tracking not implemented.
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }

  private:
    const SlotArraySharedPtr slot_array_;
    const ShardVectorSharedPtr shard_vector_;
    Random::RandomGenerator& random_;
  };

  absl::Mutex mutex_;
  SlotArraySharedPtr slot_array_ ABSL_GUARDED_BY(mutex_);
  ClusterSlotsSharedPtr current_cluster_slot_;
  ShardVectorSharedPtr shard_vector_;
  Random::RandomGenerator& random_;
};

class RedisClusterThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
public:
  RedisClusterThreadAwareLoadBalancer(Upstream::LoadBalancerFactorySharedPtr factory)
      : factory_(std::move(factory)) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override { return absl::OkStatus(); }

private:
  Upstream::LoadBalancerFactorySharedPtr factory_;
};

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
