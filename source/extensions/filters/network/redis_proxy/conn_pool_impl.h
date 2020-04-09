#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/stats/stats_macros.h"
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

#include "extensions/common/redis/cluster_refresh_manager.h"
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

#define REDIS_CLUSTER_STATS(COUNTER)                                                               \
  COUNTER(upstream_cx_drained)                                                                     \
  COUNTER(max_upstream_unknown_connections_reached)

struct RedisClusterStats {
  REDIS_CLUSTER_STATS(GENERATE_COUNTER_STRUCT)
};

class DoNothingPoolCallbacks : public PoolCallbacks {
public:
  void onResponse(Common::Redis::RespValuePtr&&) override{};
  void onFailure() override{};
};

class InstanceImpl : public Instance {
public:
  InstanceImpl(
      const std::string& cluster_name, Upstream::ClusterManager& cm,
      Common::Redis::Client::ClientFactory& client_factory, ThreadLocal::SlotAllocator& tls,
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings&
          config,
      Api::Api& api, Stats::ScopePtr&& stats_scope,
      const Common::Redis::RedisCommandStatsSharedPtr& redis_command_stats,
      Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager);
  // RedisProxy::ConnPool::Instance
  Common::Redis::Client::PoolRequest* makeRequest(const std::string& key, RespVariant&& request,
                                                  PoolCallbacks& callbacks) override;
  /**
   * Makes a redis request based on IP address and TCP port of the upstream host (e.g.,
   * moved/ask cluster redirection). This is now only kept mostly for testing.
   * @param host_address supplies the IP address and TCP port of the upstream host to receive
   * the request.
   * @param request supplies the Redis request to make.
   * @param callbacks supplies the request completion callbacks.
   * @return PoolRequest* a handle to the active request or nullptr if the request could not be
   * made for some reason.
   */
  Common::Redis::Client::PoolRequest*
  makeRequestToHost(const std::string& host_address, const Common::Redis::RespValue& request,
                    Common::Redis::Client::ClientCallbacks& callbacks);

  bool onRedirection() override { return refresh_manager_->onRedirection(cluster_name_); }
  bool onFailure() { return refresh_manager_->onFailure(cluster_name_); }
  bool onHostDegraded() { return refresh_manager_->onHostDegraded(cluster_name_); }

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

  struct PendingRequest : public Common::Redis::Client::ClientCallbacks,
                          public Common::Redis::Client::PoolRequest {
    PendingRequest(ThreadLocalPool& parent, RespVariant&& incoming_request,
                   PoolCallbacks& pool_callbacks);
    ~PendingRequest() override;

    // Common::Redis::Client::ClientCallbacks
    void onResponse(Common::Redis::RespValuePtr&& response) override;
    void onFailure() override;
    bool onRedirection(Common::Redis::RespValuePtr&& value, const std::string& host_address,
                       bool ask_redirection) override;

    // PoolRequest
    void cancel() override;

    ThreadLocalPool& parent_;
    const RespVariant incoming_request_;
    Common::Redis::Client::PoolRequest* request_handler_;
    PoolCallbacks& pool_callbacks_;
  };

  struct ThreadLocalPool : public ThreadLocal::ThreadLocalObject,
                           public Upstream::ClusterUpdateCallbacks {
    ThreadLocalPool(InstanceImpl& parent, Event::Dispatcher& dispatcher, std::string cluster_name);
    ~ThreadLocalPool() override;
    ThreadLocalActiveClientPtr& threadLocalActiveClient(Upstream::HostConstSharedPtr host);
    Common::Redis::Client::PoolRequest* makeRequest(const std::string& key, RespVariant&& request,
                                                    PoolCallbacks& callbacks);
    Common::Redis::Client::PoolRequest*
    makeRequestToHost(const std::string& host_address, const Common::Redis::RespValue& request,
                      Common::Redis::Client::ClientCallbacks& callbacks);

    void onClusterAddOrUpdateNonVirtual(Upstream::ThreadLocalCluster& cluster);
    void onHostsAdded(const std::vector<Upstream::HostSharedPtr>& hosts_added);
    void onHostsRemoved(const std::vector<Upstream::HostSharedPtr>& hosts_removed);
    void drainClients();

    // Upstream::ClusterUpdateCallbacks
    void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) override {
      onClusterAddOrUpdateNonVirtual(cluster);
    }
    void onClusterRemoval(const std::string& cluster_name) override;

    void onRequestCompleted();

    InstanceImpl& parent_;
    Event::Dispatcher& dispatcher_;
    const std::string cluster_name_;
    Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_handle_;
    Upstream::ThreadLocalCluster* cluster_{};
    std::unordered_map<Upstream::HostConstSharedPtr, ThreadLocalActiveClientPtr> client_map_;
    Envoy::Common::CallbackHandle* host_set_member_update_cb_handle_{};
    std::unordered_map<std::string, Upstream::HostConstSharedPtr> host_address_map_;
    std::string auth_password_;
    std::list<Upstream::HostSharedPtr> created_via_redirect_hosts_;
    std::list<ThreadLocalActiveClientPtr> clients_to_drain_;
    std::list<PendingRequest> pending_requests_;

    /* This timer is used to poll the active clients in clients_to_drain_ to determine whether they
     * have been drained (have no active requests) or not. It is only enabled after a client has
     * been added to clients_to_drain_, and is only re-enabled as long as that list is not empty. A
     * timer is being used as opposed to using a callback to avoid adding a check of
     * clients_to_drain_ to the main data code path as this should only rarely be not empty.
     */
    Event::TimerPtr drain_timer_;
    bool is_redis_cluster_;
  };

  const std::string cluster_name_;
  Upstream::ClusterManager& cm_;
  Common::Redis::Client::ClientFactory& client_factory_;
  ThreadLocal::SlotPtr tls_;
  Common::Redis::Client::ConfigImpl config_;
  Api::Api& api_;
  Stats::ScopePtr stats_scope_;
  Common::Redis::RedisCommandStatsSharedPtr redis_command_stats_;
  RedisClusterStats redis_cluster_stats_;
  const Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager_;
};

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
