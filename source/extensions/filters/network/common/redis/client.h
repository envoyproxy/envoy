#pragma once

#include <cstdint>

#include "envoy/upstream/cluster_manager.h"

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
   * Initialize the connection. Issue the auth command and readonly command as needed.
   * @param auth password for upstream host.
   */
  virtual void initialize(const std::string& auth_username, const std::string& auth_password) PURE;
};

using ClientPtr = std::unique_ptr<Client>;

/**
 * Read policy to use for Redis cluster.
 */
enum class ReadPolicy { Primary, PreferPrimary, Replica, PreferReplica, Any };

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
};

using ConfigSharedPtr = std::shared_ptr<Config>;

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
   * @param auth password for upstream host.
   * @param is_transaction_client true if this client was created to relay a transaction.
   * @return ClientPtr a new connection pool client.
   */
  virtual ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                           const Config& config,
                           const RedisCommandStatsSharedPtr& redis_command_stats,
                           Stats::Scope& scope, const std::string& auth_username,
                           const std::string& auth_password, bool is_transaction_client) PURE;
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
  ~Transaction() {
    if (connection_established_) {
      client_->close();
      connection_established_ = false;
    }
  }

  void start() { active_ = true; }

  void close() {
    active_ = false;
    key_.clear();
    if (connection_established_) {
      client_->close();
      connection_established_ = false;
    }
    should_close_ = false;
  }

  bool active_{false};
  bool connection_established_{false};
  bool should_close_{false};
  std::string key_;
  ClientPtr client_;
  Network::ConnectionCallbacks* connection_cb_;
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
