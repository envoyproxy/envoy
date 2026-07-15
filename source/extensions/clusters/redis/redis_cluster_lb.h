#pragma once

#include <array>
#include <limits>
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
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

static const uint64_t MaxSlot = 16384;

// Sentinel stored in ``SlotArray`` entries for slots CLUSTER SLOTS did not cover. Deliberately
// numeric_limits max rather than ``MaxSlot``: a malformed response with duplicate slot coverage
// can yield more than ``MaxSlot`` distinct shards, which would make ``MaxSlot`` a valid shard
// index and silently break ``membersForSlot``'s nullopt-for-unassigned contract (and
// ``chooseHost``'s shard-0 fallback for unassigned slots).
static constexpr uint64_t SlotUnassigned = std::numeric_limits<uint64_t>::max();

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

// Map from host address to zone (used during zone discovery)
using HostZoneMap = absl::flat_hash_map<std::string, std::string>;

class RedisLoadBalancerContext {
public:
  virtual ~RedisLoadBalancerContext() = default;

  virtual bool isReadCommand() const PURE;
  virtual NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const PURE;

  /**
   * @return the client zone for zone-aware routing.
   * Returns empty string if zone-aware routing is not configured.
   */
  virtual const std::string& clientZone() const PURE;
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
   * @param client_zone specify the client zone for zone-aware routing.
   */
  RedisLoadBalancerContextImpl(const std::string& key, bool enabled_hashtagging,
                               bool is_redis_cluster,
                               const NetworkFilters::Common::Redis::RespValue& request,
                               NetworkFilters::Common::Redis::Client::ReadPolicy read_policy =
                                   NetworkFilters::Common::Redis::Client::ReadPolicy::Primary,
                               const std::string& client_zone = "");

  // Upstream::LoadBalancerContextBase
  std::optional<uint64_t> computeHashKey() override { return hash_key_; }

  bool isReadCommand() const override { return is_read_; }

  NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const override {
    return read_policy_;
  }

  const std::string& clientZone() const override { return client_zone_; }

  // Extract the Redis Cluster hash tag ({...}) from a key, or return the whole key when hashtagging
  // is disabled or no well-formed tag is present. Stateless; exposed as a public static so
  // ``redisSlotForKey`` (and any other key/channel -> slot mapping) reuses the exact algorithm the
  // data-path LB context applies rather than re-deriving it.
  static absl::string_view hashtag(absl::string_view v, bool enabled);

private:
  static bool isReadRequest(const NetworkFilters::Common::Redis::RespValue& request);

  const std::optional<uint64_t> hash_key_;
  const bool is_read_;
  const NetworkFilters::Common::Redis::Client::ReadPolicy read_policy_;
  const std::string client_zone_;
};

class RedisSpecifyShardContextImpl : public RedisLoadBalancerContextImpl {
public:
  /**
   * The redis specify Shard load balancer context for Redis requests.
   * @param shard_index specify the shard index for the Redis request.
   * @param request specify the Redis request.
   * @param read_policy specify the read policy.
   * @param client_zone specify the client zone for zone-aware routing.
   */
  RedisSpecifyShardContextImpl(uint64_t shard_index,
                               const NetworkFilters::Common::Redis::RespValue& request,
                               NetworkFilters::Common::Redis::Client::ReadPolicy read_policy =
                                   NetworkFilters::Common::Redis::Client::ReadPolicy::Primary,
                               const std::string& client_zone = "");

  // Upstream::LoadBalancerContextBase
  std::optional<uint64_t> computeHashKey() override { return shard_index_; }

private:
  const std::optional<uint64_t> shard_index_;
};

// Map a Redis key (or a pub/sub channel name) to its cluster hash slot with the same
// crc16(hashtag(key)) % 16384 rule the data-path LB context applies. Free function so the pub/sub
// conn pool can resolve a channel's slot without building a full load-balancer context/request.
inline uint64_t redisSlotForKey(absl::string_view key) {
  return Crc16::crc16(RedisLoadBalancerContextImpl::hashtag(key, true)) % MaxSlot;
}

// A read-only snapshot of the members (primary + replicas) of the shard that owns a given slot,
// exposed via ShardMembershipResolver so the pub/sub conn pool can home SHARD_MEMBERS-placed
// subscriptions across a slot shard's replicas without depending on the load balancer's internal
// shard type. Shares the LB snapshot's host vector rather than copying it; the slot primary is
// ``all_hosts->front()`` when a caller needs it, so no separate field is carried.
struct ShardMembers {
  Upstream::HostVectorConstSharedPtr all_hosts; // primary first, then replicas — LB snapshot order.
};

// Implemented by RedisClusterLoadBalancer so a caller holding an Upstream::LoadBalancer& can
// dynamic_cast to it and query shard membership off the LB's immutable per-instance topology
// snapshot. Non-cluster load balancers do not implement it (the dynamic_cast yields nullptr, which
// the caller reads as "no membership model" and degrades to primary-only placement).
class ShardMembershipResolver {
public:
  virtual ~ShardMembershipResolver() = default;

  // The shard owning `slot`, or std::nullopt when the slot is unassigned or no topology snapshot
  // exists yet. Cold path — consulted only on subscription placement / re-placement.
  virtual std::optional<ShardMembers> membersForSlot(uint64_t slot) const PURE;
};

class ClusterSlotUpdateCallBack {
public:
  virtual ~ClusterSlotUpdateCallBack() = default;

  /**
   * Callback when cluster slot is updated
   * @param slots provides the updated cluster slots.
   * @param all_hosts provides the updated hosts (with zone info already set in host locality).
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
class RedisClusterLoadBalancerFactory
    : public ClusterSlotUpdateCallBack,
      public Upstream::LoadBalancerFactory,
      public std::enable_shared_from_this<RedisClusterLoadBalancerFactory> {
public:
  RedisClusterLoadBalancerFactory(Random::RandomGenerator& random) : random_(random) {}

  // ClusterSlotUpdateCallBack
  bool onClusterSlotUpdate(ClusterSlotsSharedPtr&& slots, Upstream::HostMap& all_hosts) override;

  void onHostHealthUpdate() override;

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;
  bool recreateOnHostChangeDeprecated() const override { return false; }

private:
  class RedisShard {
  public:
    // Constructor derives zone information from host localities
    RedisShard(Upstream::HostConstSharedPtr primary, Upstream::HostVectorConstSharedPtr replicas,
               Upstream::HostVectorConstSharedPtr all_hosts, Random::RandomGenerator& random)
        : primary_(std::move(primary)) {
      // Derive primary zone from host's locality
      primary_zone_ = primary_->locality().zone();

      replicas_.updateHosts(Upstream::HostSetImpl::partitionHosts(
                                std::move(replicas), Upstream::HostsPerLocalityImpl::empty()),
                            nullptr, {}, {}, random.random());
      all_hosts_.updateHosts(Upstream::HostSetImpl::partitionHosts(
                                 std::move(all_hosts), Upstream::HostsPerLocalityImpl::empty()),
                             nullptr, {}, {}, random.random());

      // Group replicas by zone from host localities for efficient zone-aware routing
      absl::flat_hash_map<std::string, Upstream::HostVector> zone_hosts;
      for (const auto& host : replicas_.hosts()) {
        const std::string& zone = host->locality().zone();
        if (!zone.empty()) {
          zone_hosts[zone].push_back(host);
        }
      }
      // Convert each zone's hosts to HostSetImpl
      for (auto& [zone, hosts] : zone_hosts) {
        auto host_set = std::make_unique<Upstream::HostSetImpl>(0, std::nullopt, std::nullopt);
        auto hosts_ptr = std::make_shared<Upstream::HostVector>(std::move(hosts));
        host_set->updateHosts(Upstream::HostSetImpl::partitionHosts(
                                  std::move(hosts_ptr), Upstream::HostsPerLocalityImpl::empty()),
                              nullptr, {}, {}, random.random());
        replicas_by_zone_[zone] = std::move(host_set);
      }
    }
    const Upstream::HostConstSharedPtr primary() const { return primary_; }
    const Upstream::HostSetImpl& replicas() const { return replicas_; }
    const Upstream::HostSetImpl& allHosts() const { return all_hosts_; }

    // Zone information for zone-aware routing
    const std::string& primaryZone() const { return primary_zone_; }

    // Get replicas in a specific zone. Returns null unique_ptr if no replicas in that zone.
    const std::unique_ptr<Upstream::HostSetImpl>& replicasInZone(const std::string& zone) const {
      static const std::unique_ptr<Upstream::HostSetImpl> null_ptr;
      if (zone.empty()) {
        return null_ptr;
      }
      auto it = replicas_by_zone_.find(zone);
      return (it != replicas_by_zone_.end()) ? it->second : null_ptr;
    }

  private:
    const Upstream::HostConstSharedPtr primary_;
    Upstream::HostSetImpl replicas_{0, std::nullopt, std::nullopt};
    Upstream::HostSetImpl all_hosts_{0, std::nullopt, std::nullopt};
    std::string primary_zone_;
    absl::flat_hash_map<std::string, std::unique_ptr<Upstream::HostSetImpl>> replicas_by_zone_;
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
  class RedisClusterLoadBalancer : public Upstream::LoadBalancer, public ShardMembershipResolver {
  public:
    RedisClusterLoadBalancer(std::shared_ptr<RedisClusterLoadBalancerFactory> factory,
                             const Upstream::PrioritySet& priority_set);

    // Upstream::LoadBalancerBase
    Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext*) override;
    Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
      return nullptr;
    }
    // Pool selection not implemented.
    std::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                             const Upstream::Host& /*host*/,
                             std::vector<uint8_t>& /*hash_key*/) override {
      return std::nullopt;
    }
    // Lifetime tracking not implemented.
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }

    // ShardMembershipResolver: read the shard owning `slot` off this LB instance's immutable
    // topology snapshot (same snapshot chooseHost() routes against, so a channel's placement and
    // its data-path routing agree). Returns std::nullopt when no snapshot exists or the slot is
    // unassigned / maps past the shard vector.
    std::optional<ShardMembers> membersForSlot(uint64_t slot) const override {
      if (slot_array_ == nullptr || shard_vector_ == nullptr || slot >= MaxSlot) {
        return std::nullopt;
      }
      const uint64_t idx = (*slot_array_)[slot];
      // Unassigned slots carry an out-of-range sentinel (see updateClusterSlots' ``fill``), so this
      // bound is what actually enforces the "nullopt for an unassigned slot" contract above — a
      // slot CLUSTER SLOTS never covered is not silently placed on shard 0.
      if (idx >= shard_vector_->size()) {
        return std::nullopt;
      }
      const RedisShardSharedPtr& shard = (*shard_vector_)[idx];
      // Share the shard's immutable host-vector snapshot (primary first, then replicas) rather than
      // copying it — the RedisShard is itself an immutable per-epoch snapshot, so its hostsPtr() is
      // safe to hand out by shared_ptr.
      return ShardMembers{shard->allHosts().hostsPtr()};
    }

  private:
    // Re-snapshots the topology from the parent factory under its mutex.
    void refresh();

    const std::shared_ptr<RedisClusterLoadBalancerFactory> factory_;
    SlotArraySharedPtr slot_array_;
    ShardVectorSharedPtr shard_vector_;
    Random::RandomGenerator& random_;
    ::Envoy::Common::CallbackHandlePtr member_update_cb_;
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
