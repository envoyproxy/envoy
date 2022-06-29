#pragma once

#include "source/common/http/conn_pool_base.h"
#include "source/common/http/http3/conn_pool.h"
#include "source/common/http/http_server_properties_cache_impl.h"
#include "source/common/quic/quic_stat_names.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {

// An HTTP connection pool which will handle the connectivity grid of
// [WiFi / cellular] [ipv4 / ipv6] [QUIC / TCP].
// Currently only [QUIC / TCP are handled]
class ConnectivityGrid : public ConnectionPool::Instance,
                         public Http3::PoolConnectResultCallback,
                         protected Logger::Loggable<Logger::Id::pool> {
public:
  struct ConnectivityOptions {
    explicit ConnectivityOptions(const std::vector<Http::Protocol>& protocols)
        : protocols_(protocols) {}
    std::vector<Http::Protocol> protocols_;
  };

  enum class StreamCreationResult {
    ImmediateResult,
    StreamCreationPending,
  };

  using PoolIterator = std::list<ConnectionPool::InstancePtr>::iterator;

  // This is a class which wraps a caller's connection pool callbacks to
  // auto-retry pools in the case of connection failure.
  //
  // It also relays cancellation calls between the original caller and the
  // current connection attempts.
  class WrapperCallbacks : public ConnectionPool::Cancellable,
                           public LinkedObject<WrapperCallbacks> {
  public:
    WrapperCallbacks(ConnectivityGrid& grid, Http::ResponseDecoder& decoder, PoolIterator pool_it,
                     ConnectionPool::Callbacks& callbacks, const Instance::StreamOptions& options);

    // This holds state for a single connection attempt to a specific pool.
    class ConnectionAttemptCallbacks : public ConnectionPool::Callbacks,
                                       public LinkedObject<ConnectionAttemptCallbacks> {
    public:
      ConnectionAttemptCallbacks(WrapperCallbacks& parent, PoolIterator it);
      ~ConnectionAttemptCallbacks() override;

      StreamCreationResult newStream();

      // ConnectionPool::Callbacks
      void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason,
                         Upstream::HostDescriptionConstSharedPtr host) override;
      void onPoolReady(RequestEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host,
                       StreamInfo::StreamInfo& info,
                       absl::optional<Http::Protocol> protocol) override;

      ConnectionPool::Instance& pool() { return **pool_it_; }

      void cancel(Envoy::ConnectionPool::CancelPolicy cancel_policy);

    private:
      // A pointer back up to the parent.
      WrapperCallbacks& parent_;
      // The pool for this connection attempt.
      const PoolIterator pool_it_;
      // The handle to cancel this connection attempt.
      // This is owned by the pool which created it.
      Cancellable* cancellable_;
    };
    using ConnectionAttemptCallbacksPtr = std::unique_ptr<ConnectionAttemptCallbacks>;

    // ConnectionPool::Cancellable
    void cancel(Envoy::ConnectionPool::CancelPolicy cancel_policy) override;

    // Attempt to create a new stream for pool().
    StreamCreationResult newStream();

    // Called on pool failure or timeout to kick off another connection attempt.
    // Returns true if there is a failover pool and a connection has been
    // attempted, false if all pools have been tried.
    bool tryAnotherConnection();

    // Called by a ConnectionAttempt when the underlying pool fails.
    void onConnectionAttemptFailed(ConnectionAttemptCallbacks* attempt,
                                   ConnectionPool::PoolFailureReason reason,
                                   absl::string_view transport_failure_reason,
                                   Upstream::HostDescriptionConstSharedPtr host);

    // Called by a ConnectionAttempt when the underlying pool is ready.
    void onConnectionAttemptReady(ConnectionAttemptCallbacks* attempt, RequestEncoder& encoder,
                                  Upstream::HostDescriptionConstSharedPtr host,
                                  StreamInfo::StreamInfo& info,
                                  absl::optional<Http::Protocol> protocol);

  private:
    // Removes this from the owning list, deleting it.
    void deleteThis();

    // Marks HTTP/3 broken if the HTTP/3 attempt failed but a TCP attempt succeeded.
    // While HTTP/3 is broken the grid will not attempt to make new HTTP/3 connections.
    void maybeMarkHttp3Broken();

    // Cancels any pending attempts and deletes them.
    void cancelAllPendingAttempts(Envoy::ConnectionPool::CancelPolicy cancel_policy);

    // Tracks all the connection attempts which currently in flight.
    std::list<ConnectionAttemptCallbacksPtr> connection_attempts_;

    // The owning grid.
    ConnectivityGrid& grid_;
    // The decoder for the original newStream, needed to create streams on subsequent pools.
    Http::ResponseDecoder& decoder_;
    // The callbacks from the original caller, which must get onPoolFailure or
    // onPoolReady unless there is call to cancel(). Will be nullptr if the caller
    // has been notified while attempts are still pending.
    ConnectionPool::Callbacks* inner_callbacks_;
    // The timer which tracks when new connections should be attempted.
    Event::TimerPtr next_attempt_timer_;
    // The iterator to the last pool which had a connection attempt.
    PoolIterator current_;
    // True if the HTTP/3 attempt failed.
    bool http3_attempt_failed_{};
    // True if the TCP attempt succeeded.
    bool tcp_attempt_succeeded_{};
    // Latch the passed-in stream options.
    const Instance::StreamOptions stream_options_{};
  };
  using WrapperCallbacksPtr = std::unique_ptr<WrapperCallbacks>;

  ConnectivityGrid(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                   Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                   Upstream::ClusterConnectivityState& state, TimeSource& time_source,
                   HttpServerPropertiesCacheSharedPtr alternate_protocols,
                   ConnectivityOptions connectivity_options, Quic::QuicStatNames& quic_stat_names,
                   Stats::Scope& scope, Http::PersistentQuicInfo& quic_info);
  ~ConnectivityGrid() override;

  // Event::DeferredDeletable
  void deleteIsPending() override;

  // Http::ConnPool::Instance
  bool hasActiveConnections() const override;
  ConnectionPool::Cancellable* newStream(Http::ResponseDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks,
                                         const Instance::StreamOptions& options) override;
  void addIdleCallback(IdleCb cb) override;
  bool isIdle() const override;
  void drainConnections(Envoy::ConnectionPool::DrainBehavior drain_behavior) override;
  Upstream::HostDescriptionConstSharedPtr host() const override;
  bool maybePreconnect(float preconnect_ratio) override;
  absl::string_view protocolDescription() const override { return "connection grid"; }

  // Returns the next pool in the ordered priority list.
  absl::optional<PoolIterator> nextPool(PoolIterator pool_it);

  // Returns true if pool is the grid's HTTP/3 connection pool.
  bool isPoolHttp3(const ConnectionPool::Instance& pool);

  // Returns true if HTTP/3 is currently broken. While HTTP/3 is broken the grid will not
  // attempt to make new HTTP/3 connections.
  bool isHttp3Broken() const;

  // Marks HTTP/3 broken for a period of time subject to exponential backoff. While HTTP/3
  // is broken the grid will not attempt to make new HTTP/3 connections.
  void markHttp3Broken();

  // Marks that HTTP/3 is working, which resets the exponential backoff counter in the
  // event that HTTP/3 is marked broken again.
  void markHttp3Confirmed();

  // Http3::PoolConnectResultCallback
  void onHandshakeComplete() override;
  void onZeroRttHandshakeFailed() override;

protected:
  // Set the required idle callback on the pool.
  void setupPool(ConnectionPool::Instance& pool);

private:
  friend class ConnectivityGridForTest;

  // Return origin of the remote host. If the host doesn't have an IP address,
  // the port of the origin will be 0.
  HttpServerPropertiesCache::Http3StatusTracker& getHttp3StatusTracker() const;

  // Called by each pool as it idles. The grid is responsible for calling
  // idle_callbacks_ once all pools have idled.
  void onIdleReceived();

  // Returns true if HTTP/3 should be attempted because there is an alternate protocol
  // that specifies HTTP/3 and HTTP/3 is not broken.
  bool shouldAttemptHttp3();

  // Creates the next pool in the priority list, or absl::nullopt if all pools
  // have been created.
  virtual absl::optional<PoolIterator> createNextPool();

  // This batch of member variables are latched objects required for pool creation.
  Event::Dispatcher& dispatcher_;
  Random::RandomGenerator& random_generator_;
  Upstream::HostConstSharedPtr host_;
  Upstream::ResourcePriority priority_;
  const Network::ConnectionSocket::OptionsSharedPtr options_;
  const Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  Upstream::ClusterConnectivityState& state_;
  std::chrono::milliseconds next_attempt_duration_;
  TimeSource& time_source_;
  HttpServerPropertiesCacheSharedPtr alternate_protocols_;

  // True iff this pool is draining. No new streams or connections should be created
  // in this state.
  bool draining_{false};

  // Tracks the callbacks to be called on drain completion.
  std::list<Instance::IdleCb> idle_callbacks_;

  // The connection pools to use to create new streams, ordered in the order of
  // desired use.
  std::list<ConnectionPool::InstancePtr> pools_;

  // True iff under the stack of the destructor, to avoid calling drain
  // callbacks on deletion.
  bool destroying_{};

  // True iff this pool is being being defer deleted.
  bool deferred_deleting_{};

  // Wrapped callbacks are stashed in the wrapped_callbacks_ for ownership.
  std::list<WrapperCallbacksPtr> wrapped_callbacks_;

  Quic::QuicStatNames& quic_stat_names_;
  Stats::Scope& scope_;
  // The origin for this pool.
  // Note the host name here is based off of the host name used for SNI, which
  // may be from the cluster config, or the request headers for auto-sni.
  HttpServerPropertiesCache::Origin origin_;
  Http::PersistentQuicInfo& quic_info_;
};

} // namespace Http
} // namespace Envoy
