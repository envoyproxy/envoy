#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/endpoint/endpoint.pb.h"
#include "envoy/config/cluster/redis/redis_cluster.pb.h"
#include "envoy/config/cluster/redis/redis_cluster.pb.validate.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/locality.h"
#include "envoy/upstream/upstream.h"

#include "common/common/callback_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"
#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"
#include "common/upstream/upstream_impl.h"

#include "server/transport_socket_config_impl.h"

#include "extensions/clusters/well_known_names.h"
#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/common/redis/codec.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

static const int MAX_SLOT = 16384;

typedef std::array<Upstream::HostSharedPtr, MAX_SLOT> SlotArray;

typedef std::shared_ptr<SlotArray> SlotArraySharedPtr;

class ClusterSlot {
public:
  ClusterSlot(int64_t start, int64_t end, Network::Address::InstanceConstSharedPtr master)
      : start_(start), end_(end), master_(std::move(master)) {}

  int64_t start() const { return start_; }
  int64_t end() const { return end_; }
  Network::Address::InstanceConstSharedPtr master() const { return master_; };

private:
  const int64_t start_;
  const int64_t end_;
  Network::Address::InstanceConstSharedPtr master_;
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
  RedisClusterLoadBalancer(SlotArraySharedPtr slot_array) : slot_array_(slot_array){};
  virtual ~RedisClusterLoadBalancer() = default;

  // Upstream::LoadBalancerBase
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext*) override;

private:
  SlotArraySharedPtr slot_array_;
};

/**
 * This factory is created and returned by RedisCluster's factory() method, the create() method will
 * be called on each thread to create a thread local RedisClusterLoadBalancer.
 */
class RedisClusterLoadBalancerFactory : public Upstream::LoadBalancerFactory {
public:
  RedisClusterLoadBalancerFactory();

  void onClusterSlotUpdate(const std::vector<ClusterSlot>& slots, Upstream::HostMap all_hosts);

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create() override;

private:
  absl::Mutex mutex_;
  SlotArraySharedPtr slot_array_ GUARDED_BY(mutex_);
};

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
