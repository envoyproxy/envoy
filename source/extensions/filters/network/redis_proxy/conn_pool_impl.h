#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/token_bucket_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/filter_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/redis/redis_cluster_lb.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "source/extensions/common/redis/cluster_refresh_manager.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"
#include "source/extensions/filters/network/common/redis/utility.h"
#include "source/extensions/filters/network/redis_proxy/conn_pool.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ConnPool {

// TODO(mattklein123): Circuit breaking
// TODO(rshriram): Fault injection

#define REDIS_CLUSTER_STATS(COUNTER)                                                               \
  COUNTER(upstream_cx_drained)                                                                     \
  COUNTER(max_upstream_unknown_connections_reached)                                                \
  COUNTER(connection_rate_limited)

struct RedisClusterStats {
  REDIS_CLUSTER_STATS(GENERATE_COUNTER_STRUCT)
};

class DoNothingPoolCallbacks : public PoolCallbacks {
public:
  void onResponse(Common::Redis::RespValuePtr&&) override{};
  void onFailure() override{};
};

class InstanceImpl : public Instance, public std::enable_shared_from_this<InstanceImpl> {
public:
  InstanceImpl(
      const std::string& cluster_name, Upstream::ClusterManager& cm,
      Common::Redis::Client::ClientFactory& client_factory, ThreadLocal::SlotAllocator& tls,
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings&
          config,
      Api::Api& api, Stats::ScopeSharedPtr&& stats_scope,
      const Common::Redis::RedisCommandStatsSharedPtr& redis_command_stats,
      Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager,
      const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr& dns_cache);
  // RedisProxy::ConnPool::Instance
  Common::Redis::Client::PoolRequest*
  makeRequest(const std::string& key, RespVariant&& request, PoolCallbacks& callbacks,
              Common::Redis::Client::Transaction& transaction) override;
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

  void init();

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

  struct PendingRequest
      : public Common::Redis::Client::ClientCallbacks,
        public Common::Redis::Client::PoolRequest,
        public Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks,
        public Logger::Loggable<Logger::Id::redis> {
    PendingRequest(ThreadLocalPool& parent, RespVariant&& incoming_request,
                   PoolCallbacks& pool_callbacks, Upstream::HostConstSharedPtr& host);
    ~PendingRequest() override;

    // Common::Redis::Client::ClientCallbacks
    void onResponse(Common::Redis::RespValuePtr&& response) override;
    void onFailure() override;
    void onRedirection(Common::Redis::RespValuePtr&& value, const std::string& host_address,
                       bool ask_redirection) override;

    // PoolRequest
    void cancel() override;

    // Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks
    void onLoadDnsCacheComplete(
        const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&) override;

    std::string formatAddress(const Envoy::Network::Address::Ip& ip);
    void doRedirection(Common::Redis::RespValuePtr&& value, const std::string& host_address,
                       bool ask_redirection);

    ThreadLocalPool& parent_;
    const RespVariant incoming_request_;
    Common::Redis::Client::PoolRequest* request_handler_;
    PoolCallbacks& pool_callbacks_;
    Upstream::HostConstSharedPtr host_;
    Common::Redis::RespValuePtr resp_value_;
    bool ask_redirection_;
    Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryHandlePtr
        cache_load_handle_;
  };

  struct ThreadLocalPool : public ThreadLocal::ThreadLocalObject,
                           public Upstream::ClusterUpdateCallbacks,
                           public Logger::Loggable<Logger::Id::redis> {
    ThreadLocalPool(std::shared_ptr<InstanceImpl> parent, Event::Dispatcher& dispatcher,
                    std::string cluster_name,
                    const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr& dns_cache);
    ~ThreadLocalPool() override;
    ThreadLocalActiveClientPtr& threadLocalActiveClient(Upstream::HostConstSharedPtr host);
    Common::Redis::Client::PoolRequest*
    makeRequest(const std::string& key, RespVariant&& request, PoolCallbacks& callbacks,
                Common::Redis::Client::Transaction& transaction);
    Common::Redis::Client::PoolRequest*
    makeRequestToHost(const std::string& host_address, const Common::Redis::RespValue& request,
                      Common::Redis::Client::ClientCallbacks& callbacks);

    void onClusterAddOrUpdateNonVirtual(absl::string_view cluster_name,
                                        Upstream::ThreadLocalClusterCommand& get_cluster);
    void onHostsAdded(const std::vector<Upstream::HostSharedPtr>& hosts_added);
    void onHostsRemoved(const std::vector<Upstream::HostSharedPtr>& hosts_removed);
    void drainClients();

    // Upstream::ClusterUpdateCallbacks
    void onClusterAddOrUpdate(absl::string_view cluster_name,
                              Upstream::ThreadLocalClusterCommand& get_cluster) override {
      onClusterAddOrUpdateNonVirtual(cluster_name, get_cluster);
    }
    void onClusterRemoval(const std::string& cluster_name) override;

    void onRequestCompleted();

    std::weak_ptr<InstanceImpl> parent_;
    Event::Dispatcher& dispatcher_;
    const std::string cluster_name_;
    const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_{nullptr};
    Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_handle_;
    Upstream::ThreadLocalCluster* cluster_{};
    absl::node_hash_map<Upstream::HostConstSharedPtr, ThreadLocalActiveClientPtr> client_map_;
    absl::node_hash_map<Upstream::HostConstSharedPtr, TokenBucketPtr> cx_rate_limiter_map_;
    Envoy::Common::CallbackHandlePtr host_set_member_update_cb_handle_;
    absl::node_hash_map<std::string, Upstream::HostConstSharedPtr> host_address_map_;
    std::string auth_username_;
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
    bool is_redis_cluster_{false};
    Common::Redis::Client::ClientFactory& client_factory_;
    Common::Redis::Client::ConfigSharedPtr config_;
    Stats::ScopeSharedPtr stats_scope_;
    Common::Redis::RedisCommandStatsSharedPtr redis_command_stats_;
    RedisClusterStats redis_cluster_stats_;
    const Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager_;
  };

  const std::string cluster_name_;
  Upstream::ClusterManager& cm_;
  Common::Redis::Client::ClientFactory& client_factory_;
  ThreadLocal::SlotPtr tls_;
  Common::Redis::Client::ConfigSharedPtr config_;
  Api::Api& api_;
  Stats::ScopeSharedPtr stats_scope_;
  Common::Redis::RedisCommandStatsSharedPtr redis_command_stats_;
  RedisClusterStats redis_cluster_stats_;
  const Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_{nullptr};
};

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
