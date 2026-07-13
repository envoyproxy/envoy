#pragma once

#include <cstdint>

#include "envoy/common/optref.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"
#include "source/extensions/filters/network/common/redis/redis_command_stats.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

/**
 * A handle to an outbound request.
 */
class PoolRequest {
public:
  virtual ~PoolRequest() = default;

  /**
   * Cancel the request. No further request callbacks will be called.
   */
  virtual void cancel() PURE;
};

/**
 * Outbound request callbacks.
 */
class ClientCallbacks {
public:
  virtual ~ClientCallbacks() = default;

  /**
   * Called when a pipelined response is received.
   * @param value supplies the response which is now owned by the callee.
   */
  virtual void onResponse(RespValuePtr&& value) PURE;

  /**
   * Called when a network/protocol error occurs and there is no response.
   */
  virtual void onFailure() PURE;

  /**
   * Called when a MOVED or ASK redirection error is received, and the request must be retried.
   * @param value supplies the MOVED error response
   * @param host_address supplies the redirection host address and port
   * @param ask_redirection indicates if this is a ASK redirection
   */
  virtual void onRedirection(RespValuePtr&& value, const std::string& host_address,
                             bool ask_redirection) PURE;
};

/**
 * DoNothingPoolCallbacks is used for internally generated commands whose response is
 * transparently filtered, and redirection never occurs (e.g., "asking", "auth", etc.).
 */
class DoNothingPoolCallbacks : public ClientCallbacks {
public:
  // ClientCallbacks
  void onResponse(Common::Redis::RespValuePtr&&) override {}
  void onFailure() override {}
  void onRedirection(Common::Redis::RespValuePtr&&, const std::string&, bool) override {}
};

/**
 * Callbacks for RESP3 Push messages received from upstream Redis.
 * Push messages are out-of-band server-initiated messages that are not
 * responses to any pending request (e.g., pub/sub messages).
 */
class PushMessageCallbacks {
public:
  virtual ~PushMessageCallbacks() = default;
  // @param value the RESP3 Push frame (pub/sub message, or a subscribe/unsubscribe ack).
  // @param host the upstream host this subscription connection is pinned to. Lets the registry
  //        correlate control-command acks to the per-host outstanding-command FIFO (see
  //        onUpstreamControlError) and match a SUNSUBSCRIBE ack to its exact (host, channel).
  virtual void onPushMessage(RespValuePtr&& value, const Upstream::HostConstSharedPtr& host) PURE;

  /**
   * Called when a subscription connection receives an out-of-band, non-Push control reply that has
   * no outstanding request — typically a normal error to a fire-and-forget SSUBSCRIBE/SUNSUBSCRIBE
   * (e.g. ACL denial, CLUSTERDOWN, or a MOVED/ASK observed mid-reshard). Implementations reconcile
   * subscription state (e.g. a host-scoped re-subscribe) WITHOUT tearing down the shared upstream
   * connection, since every other channel multiplexed on it must stay live. Defaults to a no-op so
   * non-subscription Push consumers need not implement it.
   * @param value the control reply frame (Error or other non-Push type).
   * @param host the upstream host this subscription connection is pinned to.
   */
  virtual void onUpstreamControlError(RespValuePtr&& /*value*/,
                                      const Upstream::HostConstSharedPtr& /*host*/) {}
};

/**
 * A single redis client connection.
 */
class Client : public Event::DeferredDeletable {
public:
  ~Client() override = default;

  /**
   * Adds network connection callbacks to the underlying network connection.
   */
  virtual void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) PURE;

  /**
   * Called to determine if the client has pending requests.
   * @return bool true if the client is processing requests or false if it is currently idle.
   */
  virtual bool active() PURE;

  /**
   * Closes the underlying network connection.
   */
  virtual void close() PURE;

  /**
   * Make a pipelined request to the remote redis server.
   * @param request supplies the RESP request to make.
   * @param callbacks supplies the request callbacks.
   * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
   *         for some reason.
   */
  virtual PoolRequest* makeRequest(const RespValue& request, ClientCallbacks& callbacks) PURE;

  /**
   * Initialize the connection. Drives the AUTH / HELLO 3 / READONLY / AWS IAM negotiation
   * pipeline owned by the implementation; until this returns and any deferred init step
   * (HELLO 3 ack, IAM token fetch) completes, user makeRequest calls are held internally.
   * @param auth_username username for upstream host (RESP2 ACL or HELLO 3 AUTH user).
   * @param auth_password password for upstream host.
   */
  virtual void initialize(const std::string& auth_username, const std::string& auth_password) PURE;

  /**
   * Send a fire-and-forget command. Unlike makeRequest, no PendingRequest is created and no
   * ClientCallbacks is invoked — used for subscription commands (``SUBSCRIBE``/``SSUBSCRIBE``/...)
   * whose acknowledgments and subsequent traffic arrive as RESP3 Push frames routed via
   * setPushCallbacks. Implementations must defer the request when the connection's init
   * pipeline (HELLO 3 / AUTH / READONLY / AWS IAM token fetch) is still in flight, replaying
   * it in original submission order alongside any held user requests once init completes,
   * and must honor any per-implementation batching/buffering policy active at the time of
   * dispatch.
   * @param request the command to send.
   */
  virtual void sendCommand(const RespValue& request) PURE;

  /**
   * Set callbacks for RESP3 Push messages received on this connection.
   * @param callbacks supplies the push message callback handler, or nullptr to clear.
   */
  virtual void setPushCallbacks(PushMessageCallbacks* callbacks) PURE;
};

using ClientPtr = std::unique_ptr<Client>;

/**
 * Read policy to use for Redis cluster.
 */
enum class ReadPolicy {
  Primary,
  PreferPrimary,
  Replica,
  PreferReplica,
  Any,
  // Zone-aware routing: prefer replicas in same zone, fallback to any replica, then primary
  LocalZoneAffinity,
  // Zone-aware routing: prefer replicas in same zone, then primary in same zone, then any
  LocalZoneAffinityReplicasAndPrimary
};

/**
 * Where the RESP3 sharded pub/sub proxy homes each channel's upstream ``SSUBSCRIBE`` within the
 * shard that owns the channel's hash slot (``pubsub_settings.subscription_placement``). Distinct
 * from ReadPolicy, which governs the DATA path — placement governs the SUBSCRIBE path only.
 */
enum class SubscriptionPlacement {
  // Home every channel on its slot's primary (the default; the v1 behavior).
  Primary,
  // Spread channels across the slot shard's members (primary + replicas), least-loaded first, to
  // offload the primary's cluster-bus fan-out. Redis Cluster upstreams only; a non-cluster upstream
  // has no shard-membership model and the conn pool degrades it to Primary (with a one-time warning).
  ShardMembers
};

// Historical hardcoded defaults for the RESP3 pub/sub tuning knobs (``pubsub_settings``), the
// SINGLE source of truth for all three consumers so a change here can never drift them apart (§6):
// the connection-pool config (ConfigImpl) reads them via PROTOBUF_GET_MS_OR_DEFAULT to build the
// runtime values, the redis_proxy filter config validates the EFFECTIVE durations against the same
// defaults, and SubscriptionRegistry's ctor default arguments (test/omit-caller convenience) alias
// them. Each knob falls back to its default when unset, so configs predating ``pubsub_settings``
// are unaffected (A-7).
constexpr uint32_t kDefaultSubscribeAckTimeoutMs = 10000;
constexpr uint32_t kDefaultResubscribeBackoffBaseMs = 100;
constexpr uint32_t kDefaultResubscribeBackoffMaxMs = 30000;

/**
 * Configuration for a redis connection pool.
 */
class Config {
public:
  virtual ~Config() = default;

  /**
   * @return std::chrono::milliseconds the timeout for an individual redis operation. Currently,
   *         all operations use the same timeout.
   */
  virtual std::chrono::milliseconds opTimeout() const PURE;

  /**
   * @return bool disable outlier events even if the cluster has it enabled. This is used by the
   * healthchecker's connection pool to avoid double counting active healthcheck operations as
   * passive healthcheck operations.
   */
  virtual bool disableOutlierEvents() const PURE;

  /**
   * @return when enabled, a hash tagging function will be used to guarantee that keys with the
   * same hash tag will be forwarded to the same upstream.
   */
  virtual bool enableHashtagging() const PURE;

  /**
   * @return when enabled, moved/ask redirection errors from upstream redis servers will be
   * processed.
   */
  virtual bool enableRedirection() const PURE;

  /**
   * @return buffer size for batching commands for a single upstream host.
   */
  virtual uint32_t maxBufferSizeBeforeFlush() const PURE;

  /**
   * @return timeout for batching commands for a single upstream host.
   */
  virtual std::chrono::milliseconds bufferFlushTimeoutInMs() const PURE;

  /**
   * @return the maximum number of upstream connections to unknown hosts when enableRedirection() is
   * true.
   *
   * This value acts as an upper bound on the number of servers in a cluster if only a subset
   * of the cluster's servers are known via configuration (cluster size - number of servers in
   * cluster known to cluster manager <= maxUpstreamUnknownConnections() for proper operation).
   * Redirection errors are processed if enableRedirection() is true, and a new upstream connection
   * to a previously unknown server will be made as a result of redirection if the number of unknown
   * server connections is currently less than maxUpstreamUnknownConnections(). If a connection
   * cannot be made, then the original redirection error will be passed though unchanged to the
   * downstream client. If a cluster is using the Redis cluster protocol (RedisCluster), then the
   * cluster logic will periodically discover all of the servers in the cluster; this should
   * minimize the need for a large maxUpstreamUnknownConnections() value.
   */
  virtual uint32_t maxUpstreamUnknownConnections() const PURE;

  /**
   * @return when enabled, upstream cluster per-command statistics will be recorded.
   */
  virtual bool enableCommandStats() const PURE;

  /**
   * @return the read policy the proxy should use.
   */
  virtual ReadPolicy readPolicy() const PURE;

  virtual bool connectionRateLimitEnabled() const PURE;
  virtual uint32_t connectionRateLimitPerSec() const PURE;

  /**
   * @return how long to wait for an upstream ``SSUBSCRIBE`` ack before failing the downstream
   * pub/sub subscribe / re-resolving a re-subscribe generation (RESP3 sharded pub/sub).
   */
  virtual std::chrono::milliseconds subscribeAckTimeout() const PURE;

  /**
   * @return the base (minimum) interval of the jittered exponential backoff used to re-subscribe
   * pub/sub channels after an upstream subscription connection drop or rejected re-subscribe.
   */
  virtual std::chrono::milliseconds resubscribeBackoffBaseInterval() const PURE;

  /**
   * @return the maximum interval the pub/sub re-subscribe backoff escalates to.
   */
  virtual std::chrono::milliseconds resubscribeBackoffMaxInterval() const PURE;

  /**
   * @return where the proxy homes each pub/sub channel's upstream ``SSUBSCRIBE`` within its slot's
   * shard (RESP3 sharded pub/sub). ``ShardMembers`` on a non-cluster upstream is degraded to
   * ``Primary`` by the conn pool.
   */
  virtual SubscriptionPlacement subscriptionPlacement() const PURE;
};

using ConfigSharedPtr = std::shared_ptr<const Config>;

/**
 * A factory for individual redis client connections.
 */
class ClientFactory {
public:
  virtual ~ClientFactory() = default;

  /**
   * Create a client given an upstream host.
   * @param host supplies the upstream host.
   * @param dispatcher supplies the owning thread's dispatcher.
   * @param config supplies the connection pool configuration.
   * @param redis_command_stats supplies the redis command stats.
   * @param scope supplies the stats scope.
   * @param auth_username auth username for upstream host (empty when unused).
   * @param auth_password auth password for upstream host (empty when unused).
   * @param is_transaction_client true if this client was created to relay a transaction.
   * @param aws_iam_config supplies the AWS IAM configuration from protobuf
   * @param aws_iam_authenticator supplies the AWS IAM authenticator created during config
   * @param upstream_protocol_version selects the upstream RESP protocol negotiated on the new
   *        connection. ``Resp3`` triggers a ``HELLO 3`` handshake from ``ClientImpl::initialize``;
   *        ``Resp2`` keeps the legacy behavior (no HELLO).
   * @param upstream_resp3_hello_failure optional counter incremented on every HELLO 3 negotiation
   *        failure (error reply, wrong reply shape, redirection, network failure). Empty for
   *        callers that do not own a per-cluster stat for it (e.g. the redis health checker and
   *        cluster discovery, which always negotiate RESP2 and never trigger the counter).
   * @return ClientPtr a new connection pool client.
   */
  virtual ClientPtr
  create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
         const ConfigSharedPtr& config, const RedisCommandStatsSharedPtr& redis_command_stats,
         Stats::Scope& scope, const std::string& auth_username, const std::string& auth_password,
         bool is_transaction_client,
         std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
         std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
             aws_iam_authenticator,
         Common::Redis::RespProtocolVersion upstream_protocol_version,
         OptRef<Stats::Counter> upstream_resp3_hello_failure) PURE;
};

// A MULTI command sent when starting a transaction.
struct MultiRequest : public Extensions::NetworkFilters::Common::Redis::RespValue {
public:
  MultiRequest() {
    type(Extensions::NetworkFilters::Common::Redis::RespType::Array);
    std::vector<NetworkFilters::Common::Redis::RespValue> values(1);
    values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[0].asString() = "MULTI";
    asArray().swap(values);
  }
};

// An empty array sent when a transaction is empty.
struct EmptyArray : public Extensions::NetworkFilters::Common::Redis::RespValue {
public:
  EmptyArray() {
    type(Extensions::NetworkFilters::Common::Redis::RespType::Array);
    std::vector<NetworkFilters::Common::Redis::RespValue> values;
    asArray().swap(values);
  }
};

// A struct representing a Redis transaction.

struct Transaction {
  Transaction(Network::ConnectionCallbacks* connection_cb) : connection_cb_(connection_cb) {}
  ~Transaction() { close(); }

  void start() { active_ = true; }

  void close() {
    active_ = false;
    key_.clear();
    if (connection_established_) {
      for (auto& client : clients_) {
        client->close();
      }
      connection_established_ = false;
    }
    should_close_ = false;
  }

  bool active_{false};
  bool connection_established_{false};
  bool should_close_{false};

  // The key which represents the transaction hash slot.
  std::string key_;
  // clients_[0] represents the main connection, clients_[1..n] are for the mirroring policies.
  std::vector<ClientPtr> clients_;
  Network::ConnectionCallbacks* connection_cb_;

  // This index represents the current client on which traffic is being sent to.
  // When sending to the main redis server it will be 0, and when sending to one of
  // the mirror servers it will be 1..n.
  uint32_t current_client_idx_{0};
};

class NoOpTransaction : public Transaction {
public:
  NoOpTransaction() : Transaction(nullptr) {}
};

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
