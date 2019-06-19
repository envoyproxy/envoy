#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"
#include "common/network/filter_impl.h"
#include "common/protobuf/utility.h"
#include "common/singleton/const_singleton.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "source/extensions/clusters/redis/redis_cluster_lb.h"

#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/common/redis/codec_impl.h"
#include "extensions/filters/network/common/redis/utility.h"
#include "extensions/filters/network/redis_proxy/conn_pool.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ConnPool {

// TODO(mattklein123): Circuit breaking
// TODO(rshriram): Fault injection

class InstanceImpl : public Instance {
public:
  InstanceImpl(
      const std::string& cluster_name, Upstream::ClusterManager& cm,
      Common::Redis::Client::ClientFactory& client_factory, ThreadLocal::SlotAllocator& tls,
      const envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings& config,
      Api::Api& api, Stats::SymbolTable& symbol_table);
  // RedisProxy::ConnPool::Instance
  Common::Redis::Client::PoolRequest*
  makeRequest(const std::string& key, const Common::Redis::RespValue& request,
              Common::Redis::Client::PoolCallbacks& callbacks) override;
  Common::Redis::Client::PoolRequest*
  makeRequestToHost(const std::string& host_address, const Common::Redis::RespValue& request,
                    Common::Redis::Client::PoolCallbacks& callbacks) override;
  Stats::SymbolTable& symbolTable() { return symbol_table_; }

  // Allow the unit test to have access to private members.
  friend class RedisConnPoolImplTest;

private:
  struct ThreadLocalPool;

  struct ThreadLocalActiveClient : public Network::ConnectionCallbacks {
    ThreadLocalActiveClient(ThreadLocalPool& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ThreadLocalPool& parent_;
    Upstream::HostConstSharedPtr host_;
    Common::Redis::Client::ClientPtr redis_client_;
  };

  using ThreadLocalActiveClientPtr = std::unique_ptr<ThreadLocalActiveClient>;

  struct ThreadLocalPool : public ThreadLocal::ThreadLocalObject,
                           public Upstream::ClusterUpdateCallbacks {
    ThreadLocalPool(InstanceImpl& parent, Event::Dispatcher& dispatcher, std::string cluster_name);
    ~ThreadLocalPool();
    ThreadLocalActiveClientPtr& threadLocalActiveClient(Upstream::HostConstSharedPtr host);
    Common::Redis::Client::PoolRequest*
    makeRequest(const std::string& key, const Common::Redis::RespValue& request,
                Common::Redis::Client::PoolCallbacks& callbacks);
    Common::Redis::Client::PoolRequest*
    makeRequestToHost(const std::string& host_address, const Common::Redis::RespValue& request,
                      Common::Redis::Client::PoolCallbacks& callbacks);
    void onClusterAddOrUpdateNonVirtual(Upstream::ThreadLocalCluster& cluster);
    void onHostsRemoved(const std::vector<Upstream::HostSharedPtr>& hosts_removed);

    // Upstream::ClusterUpdateCallbacks
    void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) override {
      onClusterAddOrUpdateNonVirtual(cluster);
    }
    void onClusterRemoval(const std::string& cluster_name) override;

    InstanceImpl& parent_;
    Event::Dispatcher& dispatcher_;
    const std::string cluster_name_;
    Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_handle_;
    Upstream::ThreadLocalCluster* cluster_{};
    std::unordered_map<Upstream::HostConstSharedPtr, ThreadLocalActiveClientPtr> client_map_;
    Envoy::Common::CallbackHandle* host_set_member_update_cb_handle_{};
    std::unordered_map<std::string, Upstream::HostConstSharedPtr> host_address_map_;
    std::string auth_password_;
  };

  Upstream::ClusterManager& cm_;
  Common::Redis::Client::ClientFactory& client_factory_;
  ThreadLocal::SlotPtr tls_;
  Common::Redis::Client::ConfigImpl config_;
  Api::Api& api_;
  Stats::SymbolTable& symbol_table_;
};

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
