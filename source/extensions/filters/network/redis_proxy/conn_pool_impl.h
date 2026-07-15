#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
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
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/redis/redis_cluster_lb.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "source/extensions/common/redis/cluster_refresh_manager.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"
#include "source/extensions/filters/network/common/redis/utility.h"
#include "source/extensions/filters/network/redis_proxy/conn_pool.h"
#include "source/extensions/filters/network/redis_proxy/subscription_registry.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"

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
  COUNTER(connection_rate_limited)                                                                 \
  COUNTER(upstream_resp3_hello_failure)

struct RedisClusterStats {
  REDIS_CLUSTER_STATS(GENERATE_COUNTER_STRUCT)
};

class DoNothingPoolCallbacks : public PoolCallbacks {
public:
  void onResponse(Common::Redis::RespValuePtr&&) override {};
  void onFailure() override {};
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
      const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr& dns_cache,
      std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
      std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
          aws_iam_authenticator,
      const std::string& local_zone, Common::Redis::RespProtocolVersion protocol_version);
  uint16_t shardSize() override;
  // RedisProxy::ConnPool::Instance
  Common::Redis::Client::PoolRequest*
  makeRequest(const std::string& key, RespVariant&& request, PoolCallbacks& callbacks,
              Common::Redis::Client::Transaction& transaction) override;
  Common::Redis::Client::PoolRequest*
  makeRequestToShard(uint16_t shard_index, RespVariant&& request, PoolCallbacks& callbacks,
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
  SubscriptionRegistryPtr subscriptionRegistryShared() override;
  void init();

  // Allow the unit test to have access to private members.
  friend class RedisConnPoolImplTest;

private:
  struct ThreadLocalPool;

  struct ThreadLocalActiveClient : public Network::ConnectionCallbacks {
    // Defined out-of-line: connection_id_ is stamped from parent.next_connection_id_, which needs
    // the (later-declared) ThreadLocalPool to be complete.
    ThreadLocalActiveClient(ThreadLocalPool& parent);

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ThreadLocalPool& parent_;
    Upstream::HostConstSharedPtr host_;
    Common::Redis::Client::ClientPtr redis_client_;
    // why this connection is being torn down, stamped on the wrapper by the caller rather than
    // reconstructed in onEvent from a separate per-thread container. A PLANNED subscription-host
    // removal (onHostsRemoved) sets this true so the close-driven onEvent can tell it apart from a
    // genuine connection loss and skip its redundant resubscribe (the onClusterTopologyChange the
    // member-update posts already re-routes the moved channels). Only meaningful for a
    // subscription connection; the data path ignores it.
    bool planned_removal_{false};
    // Monotonic, never-reused id for THIS connection, stamped from
    // parent_.next_connection_id_ at construction. The stable token for deferred subscription
    // closes: a raw ThreadLocalActiveClient* would be vulnerable to address reuse once the
    // wrapper is deferred-deleted (a fresh client could land at the same address and compare
    // equal), so the pending-close map keys on this id, not the pointer.
    uint64_t connection_id_;
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
    // True only during an in-flight async DNS-redirect (the ``Loading`` window):
    // ``request_handler_`` has been cleared (the client-side request was already popped) but
    // the request is NOT complete; ``cache_load_handle_`` is the live continuation. Marks the entry
    // "pending but not pop-able" so onRequestCompleted does not destroy it out from under the DNS
    // callback (hang). Both cancel() and the dtor cancel the DNS lookup via cache_load_handle_
    // so its continuation can never fire into freed callbacks (UAF); they differ on the terminal
    // callback: cancel() clears this flag and delivers NONE (the caller abandoned the request),
    // whereas the dtor with the flag still set (cluster removal / pool teardown, no cancel)
    // delivers onFailure so a still-alive downstream is not left hanging.
    bool in_dns_redirect_{false};
  };

  struct ThreadLocalPool : public ThreadLocal::ThreadLocalObject,
                           public Upstream::ClusterUpdateCallbacks,
                           public RedisProxy::UpstreamSubscriptionCallbacks,
                           public Logger::Loggable<Logger::Id::redis> {
    ThreadLocalPool(
        std::shared_ptr<InstanceImpl> parent, Event::Dispatcher& dispatcher,
        std::string cluster_name, Api::Api& api,
        const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr& dns_cache,
        std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
        std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
            aws_iam_authenticator);
    ~ThreadLocalPool() override;
    ThreadLocalActiveClient* threadLocalActiveClient(Upstream::HostConstSharedPtr host);
    // Same as threadLocalActiveClient but keyed into subscription_client_map_ so subscription
    // traffic gets its own connection, never shared with data requests.
    ThreadLocalActiveClient* threadLocalActiveSubscriptionClient(Upstream::HostConstSharedPtr host);
    // Shared create-or-lookup body for the two maps above (client_map_ / subscription_client_map_).
    // Returns the live client, or nullptr when the host's connection rate limit denied a new
    // connection — in which case the helper ERASES the null placeholder it inserted for the lookup
    // before returning, so no caller (nor teardown) ever observes a null entry in either map.
    ThreadLocalActiveClient* getOrCreateClientInMap(
        absl::node_hash_map<Upstream::HostConstSharedPtr, ThreadLocalActiveClientPtr>& client_map,
        Upstream::HostConstSharedPtr host);
    uint16_t shardSize();
    Common::Redis::Client::PoolRequest*
    makeRequest(const std::string& key, RespVariant&& request, PoolCallbacks& callbacks,
                Common::Redis::Client::Transaction& transaction);
    Common::Redis::Client::PoolRequest*
    makeRequestToHost(Upstream::HostConstSharedPtr& host, RespVariant&& request,
                      PoolCallbacks& callbacks, Common::Redis::Client::Transaction& transaction);
    Common::Redis::Client::PoolRequest*
    makeRequestToHost(const std::string& host_address, const Common::Redis::RespValue& request,
                      Common::Redis::Client::ClientCallbacks& callbacks);
    Common::Redis::Client::PoolRequest*
    makeRequestToShard(uint16_t shard_index, RespVariant&& request, PoolCallbacks& callbacks,
                       Common::Redis::Client::Transaction& transaction);
    void onClusterAddOrUpdateNonVirtual(absl::string_view cluster_name,
                                        Upstream::ThreadLocalClusterCommand& get_cluster);
    void onHostsAdded(const std::vector<Upstream::HostSharedPtr>& hosts_added);
    void onHostsRemoved(const std::vector<Upstream::HostSharedPtr>& hosts_removed);
    void drainClients();
    ThreadLocalActiveClient*
    getOrCreateSubscriptionClient(Upstream::HostConstSharedPtr host,
                                  Common::Redis::Client::PushMessageCallbacks& push_callbacks);

    // Upstream::ClusterUpdateCallbacks
    void onClusterAddOrUpdate(absl::string_view cluster_name,
                              Upstream::ThreadLocalClusterCommand& get_cluster) override {
      onClusterAddOrUpdateNonVirtual(cluster_name, get_cluster);
    }
    void onClusterRemoval(absl::string_view cluster_name) override;

    void onRequestCompleted();

    // Close every upstream connection this pool owns — data, subscription, and draining — by
    // repeatedly closing the front of each map/set (close() erases the entry, so the loops drain).
    // Shared by ~ThreadLocalPool and onClusterRemoval, which previously open-coded the identical
    // three-loop stanza. Each caller still runs its own registry teardown and resubscribe-timer
    // handling around this — those legitimately differ (the dtor also drains pending_requests_; the
    // two disable the resubscribe timer on opposite sides of the registry reset for reasons each
    // documents), so only the common close stanza is factored out.
    void closeAllClients();

    // Per-host dedicated subscription connections (node_hash_map for pointer/reference stability
    // across inserts and erases — see subscription_client_map_).
    using SubscriptionClientMap =
        absl::node_hash_map<Upstream::HostConstSharedPtr, ThreadLocalActiveClientPtr>;

    // UpstreamSubscriptionCallbacks
    Upstream::HostConstSharedPtr chooseUpstreamHostForChannel(const std::string& channel) override;
    bool hostServesChannelSlot(const std::string& channel,
                               const Upstream::HostConstSharedPtr& host) override;
    bool sendUpstreamSsubscribeToHost(const std::string& channel,
                                      Common::Redis::Client::PushMessageCallbacks& push_callbacks,
                                      const Upstream::HostConstSharedPtr& host) override;
    UpstreamSubscriptionCallbacks::SunsubscribeResult
    sendUpstreamSunsubscribe(const std::string& channel,
                             Upstream::HostConstSharedPtr host) override;
    void scheduleResubscribe(std::chrono::milliseconds delay) override;
    bool resubscribeTimerPending() const override {
      return resubscribe_timer_ != nullptr && resubscribe_timer_->enabled();
    }
    void cancelResubscribeTimer() override {
      if (resubscribe_timer_ != nullptr) {
        resubscribe_timer_->disableTimer();
      }
    }
    void requestTopologyRefresh() override;
    bool retireSubscriptionConnectionIfIdle(const Upstream::HostConstSharedPtr& host) override;
    void closeSubscriptionConnection(const Upstream::HostConstSharedPtr& host) override;
    // Fires deferred_sub_close_timer_: closes each host queued by closeSubscriptionConnection.
    void flushDeferredSubscriptionCloses();
    // Retire ``host``'s now-idle subscription connection when its last channel unsubscribes.
    // Returns true iff the connection was retired (the host had no remaining subscriptions), so the
    // caller can report ConnectionRetired to the registry — a retired connection never acks the
    // SUNSUBSCRIBE. ``client_it`` is the caller's already-resolved subscription_client_map_ entry
    // for ``host`` (both call sites just looked it up to send/guard); reusing it avoids a second
    // lookup. Precondition: ``client_it`` points to a live (non-null) client and is not
    // dereferenced by the caller after this returns. It stays valid across the
    // hostHasSubscriptions() query below — that reads the registry's channel index, never
    // subscription_client_map_, so no insert/rehash intervenes — and subscription_client_map_ is a
    // node_hash_map, so the only entry this can erase is ``client_it``'s own (the retire branch),
    // which the caller no longer touches.
    bool maybeCleanupSubscriptionMode(const Upstream::HostConstSharedPtr& host,
                                      SubscriptionClientMap::iterator client_it);

    // Post ``fn`` to run against ``registry`` on the next event-loop iteration, capturing the
    // registry WEAKLY: on worker shutdown a still-pending post must not hold the last
    // registry ref, or its destruction while the dispatcher is being torn down would unregister the
    // registry's subscribe-ack timer against an already-freed dispatcher (UAF). If the pool
    // released the registry first, the post simply no-ops. Centralizes the weak-post boilerplate
    // the connection-loss and topology-change paths share.
    void postToRegistry(const SubscriptionRegistryPtr& registry,
                        std::function<void(SubscriptionRegistry&)> fn);

    std::weak_ptr<InstanceImpl> parent_;
    Event::Dispatcher& dispatcher_;
    const std::string cluster_name_;
    Api::Api& api_;
    const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_{nullptr};
    Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_handle_;
    Upstream::ThreadLocalCluster* cluster_{};
    // Monotonic source of ThreadLocalActiveClient::connection_id_ — a never-reused id for every
    // connection this pool opens (data or subscription). Backs the deferred-close identity check so
    // it survives a wrapper being freed and a new one reusing its address.
    uint64_t next_connection_id_{1};
    absl::node_hash_map<Upstream::HostConstSharedPtr, ThreadLocalActiveClientPtr> client_map_;
    // Subscription connections, one per host, kept OUT of client_map_ so data requests never
    // share them. A subscription connection carries only (S)SUBSCRIBE/(S)UNSUBSCRIBE and thus has
    // an empty pending_requests_; an in-band (non-Push) reply on it is Redis's error reply to a
    // fire-and-forget SSUBSCRIBE/SUNSUBSCRIBE, which ClientImpl::onRespValue routes to
    // onUpstreamControlError. The connection is KEPT open (closing it would re-issue and drop its
    // healthy channels): a correlated error fails that channel's pending subscribers (or refreshes
    // the slot map on a redirect), and an uncorrelated one re-resolves this host's channels on
    // backoff. (The protocol-error close for an unsolicited reply is the DATA-connection path — a
    // connection with no push callbacks installed — which a live subscription connection never
    // hits.)
    SubscriptionClientMap subscription_client_map_;
    // (A planned subscription-host removal is now flagged on the closing wrapper itself —
    // ThreadLocalActiveClient::planned_removal_ — instead of a separate host set here.)
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
    // The cluster load balancer's shard-membership interface, resolved once per cluster
    // add/update alongside is_redis_cluster_ and cleared on removal, so the replica-capable
    // read-policy placement callbacks are a pointer test instead of a per-call dynamic_cast. Null
    // for a non-cluster load balancer or before the cluster is present. Safe to cache because the
    // LB is not re-created on host changes
    // (RedisClusterLoadBalancerFactory::recreateOnHostChangeDeprecated() is false).
    const Clusters::Redis::ShardMembershipResolver* shard_membership_resolver_{nullptr};
    Common::Redis::Client::ClientFactory& client_factory_;
    Common::Redis::Client::ConfigSharedPtr config_;
    Stats::ScopeSharedPtr stats_scope_;
    Common::Redis::RedisCommandStatsSharedPtr redis_command_stats_;
    RedisClusterStats redis_cluster_stats_;
    const Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager_;
    std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator_;
    std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config_;
    std::string client_zone_; // Zone from node.locality.zone
    // Mirrors InstanceImpl::protocol_version_; drives upstream HELLO 3 emission.
    Common::Redis::RespProtocolVersion upstream_protocol_version_;
    // Pub/sub state. The registry is created lazily on the first subscriber. The resubscribe
    // timer's callback reads subscription_registry_ directly (nulled + timer disabled together on
    // teardown), so no separate callback/strong-ref bookkeeping is needed to keep the registry
    // alive.
    SubscriptionRegistryPtr subscription_registry_;
    Event::TimerPtr resubscribe_timer_;
    // Subscription connections closeSubscriptionConnection queued for a DEFERRED close (the
    // call arrives on the connection's own reply stack, so it cannot close inline). Maps host
    // -> the connection_id_ of the SPECIFIC client current when the close was scheduled, NOT
    // just the host: by fire time this host's subscription connection may already have been
    // replaced (retire + re-subscribe), and that replacement is healthy AND the offender gone
    // (its own close reclaimed the leak), so flush closes only if the current client's
    // connection_id_ still matches. Keying on the monotonic id rather than a raw pointer
    // sidesteps address reuse: a fresh wrapper at the offender's old address gets a NEW id, so
    // it never compares equal. The timer is a pool member, so destroying the pool cancels any
    // pending fire — no close can outlive the pool.
    absl::flat_hash_map<Upstream::HostConstSharedPtr, uint64_t> hosts_pending_sub_close_;
    Event::TimerPtr deferred_sub_close_timer_;
  };

  const std::string& localZone() const { return local_zone_; }

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
  std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
      aws_iam_authenticator_;
  std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config_;
  const std::string local_zone_; // Zone from node.locality.zone
  // Listener-level RESP version, mirrored into each ThreadLocalPool on slot creation.
  const Common::Redis::RespProtocolVersion protocol_version_;
};

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
