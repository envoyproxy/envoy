#pragma once

#include <array>
#include <string>
#include <vector>

#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "common/network/address_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "source/extensions/clusters/redis/crc16.h"

#include "extensions/clusters/well_known_names.h"
#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/codec.h"
#include "extensions/filters/network/common/redis/supported_commands.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

static const uint64_t MaxSlot = 16384;

struct RedisShard {
  Upstream::HostSharedPtr master_;
  // TODO(hyang): figure out how to update the health state
  Upstream::HostSetImpl replicas_{0, absl::nullopt};
  Upstream::HostSetImpl all_hosts_{0, absl::nullopt};
};

typedef std::shared_ptr<RedisShard> RedisShardSharedPtr;
typedef std::array<RedisShardSharedPtr, MaxSlot> SlotArray;
typedef std::shared_ptr<const SlotArray> SlotArraySharedPtr;

/**
 * A map of master address to ClusterSlotData.
 */
typedef absl::flat_hash_map<std::string, RedisShardSharedPtr> ShardMap;

class ClusterSlot {
public:
  ClusterSlot(int64_t start, int64_t end, Network::Address::InstanceConstSharedPtr master)
      : start_(start), end_(end), master_(std::move(master)) {}

  int64_t start() const { return start_; }
  int64_t end() const { return end_; }
  Network::Address::InstanceConstSharedPtr master() const { return master_; }
  const absl::flat_hash_set<Network::Address::InstanceConstSharedPtr>& slaves() const {
    return slaves_;
  }
  void addSlave(Network::Address::InstanceConstSharedPtr slave_address) {
    slaves_.insert(std::move(slave_address));
  }

  bool operator==(const ClusterSlot& rhs) const;

private:
  int64_t start_;
  int64_t end_;
  Network::Address::InstanceConstSharedPtr master_;
  absl::flat_hash_set<Network::Address::InstanceConstSharedPtr> slaves_;
};

typedef std::unique_ptr<std::vector<ClusterSlot>> ClusterSlotsPtr;

class RedisLoadBalancerContext {
public:
  virtual ~RedisLoadBalancerContext() = default;

  virtual bool isReadCommand() const PURE;
  virtual NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const PURE;
};

class RedisLoadBalancerContextImpl : public RedisLoadBalancerContext,
                                     public Upstream::LoadBalancerContextBase {
public:
  RedisLoadBalancerContextImpl(const std::string& key, bool enabled_hashtagging, bool use_crc16,
                               const NetworkFilters::Common::Redis::RespValue& request,
                               NetworkFilters::Common::Redis::Client::ReadPolicy read_policy =
                                   NetworkFilters::Common::Redis::Client::ReadPolicy::Master);

  // Upstream::LoadBalancerContextBase
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }

  bool isReadCommand() const override { return is_read_; }

  NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const override {
    return read_policy_;
  }

private:
  absl::string_view hashtag(absl::string_view v, bool enabled);

  const absl::optional<uint64_t> hash_key_;
  const bool is_read_;
  const NetworkFilters::Common::Redis::Client::ReadPolicy read_policy_;
};

/*
 * This class implements load balancing according to `Redis Cluster
 * <https://redis.io/topics/cluster-spec>`_. This load balancer is thread local and created through
 * the RedisClusterLoadBalancerFactory by the cluster manager.
 *
 * The topology is stored in cluster_slots_map_. According to the
 * `Redis Cluster Spec <https://redis.io/topics/cluster-spec#keys-distribution-model`_, the key
 * space is split into a fixed size 16384 slots. The current implementation uses a fixed size
 * std::array() of shared_ptr pointing to the master host. This has a fixed cpu and memory cost and
 * provide a fast lookup constant time lookup similar to Maglev. This will be used by the redis
 * proxy filter for load balancing purpose.
 */
class RedisClusterLoadBalancer : public Upstream::LoadBalancer {
public:
  RedisClusterLoadBalancer(SlotArraySharedPtr slot_array, Runtime::RandomGenerator& random)
      : slot_array_(std::move(slot_array)), random_(random) {}

  // Upstream::LoadBalancerBase
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext*) override;

private:
  SlotArraySharedPtr slot_array_;
  Runtime::RandomGenerator& random_;
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
  virtual bool onClusterSlotUpdate(ClusterSlotsPtr&& slots, Upstream::HostMap all_hosts) PURE;
};

using ClusterSlotUpdateCallBackSharedPtr = std::shared_ptr<ClusterSlotUpdateCallBack>;

/**
 * This factory is created and returned by RedisCluster's factory() method, the create() method will
 * be called on each thread to create a thread local RedisClusterLoadBalancer.
 */
class RedisClusterLoadBalancerFactory : public ClusterSlotUpdateCallBack,
                                        public Upstream::LoadBalancerFactory {
public:
  RedisClusterLoadBalancerFactory(Runtime::RandomGenerator& random) : random_(random) {}

  // ClusterSlotUpdateCallBack
  bool onClusterSlotUpdate(ClusterSlotsPtr&& slots, Upstream::HostMap all_hosts) override;

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create() override;

private:
  absl::Mutex mutex_;
  SlotArraySharedPtr slot_array_ GUARDED_BY(mutex_);
  ClusterSlotsPtr current_cluster_slot_;
  Runtime::RandomGenerator& random_;
};

class RedisClusterThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
public:
  RedisClusterThreadAwareLoadBalancer(Upstream::LoadBalancerFactorySharedPtr factory)
      : factory_(std::move(factory)) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  void initialize() override{};

private:
  Upstream::LoadBalancerFactorySharedPtr factory_;
};

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
