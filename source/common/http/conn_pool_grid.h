#pragma once

#include "source/common/http/conn_pool_base.h"
#include "source/common/http/http3/conn_pool.h"
#include "source/common/http/http_server_properties_cache_impl.h"
#include "source/common/quic/quic_stat_names.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Http {

// An HTTP connection pool which handles HTTP/3 failing over to HTTP/2
//
// The ConnectivityGrid wraps an inner HTTP/3 and HTTP/2 pool.
//
// each newStream attempt to the grid creates a wrapper callback which attempts
// to hide from the caller that there may be serial or parallel newStream calls
// to the HTTP/3 and HTTP/2 pools.
//
// Any cancel call on the wrapper callbacks cancels either or both stream
// attempts to the wrapped pools. the wrapper callbacks will only pass up
// failure if both HTTP/3 and HTTP/2 attempts fail.
//
// The grid also handles HTTP/3 "happy eyeballs" which is a best-effort attempt
// to try using one IPv4 address and one IPv6 address if both families exist in
// the host's address list.
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

  // This is a class which wraps a caller's connection pool callbacks to
  // auto-retry pools in the case of connection failure.
  //
  // It also relays cancellation calls between the original caller and the
  // current connection attempts.
  class WrapperCallbacks : public ConnectionPool::Cancellable,
                           public LinkedObject<WrapperCallbacks>,
                           public Event::DeferredDeletable {
  public:
    WrapperCallbacks(ConnectivityGrid& grid, Http::ResponseDecoder& decoder,
                     ConnectionPool::Callbacks& callbacks, const Instance::StreamOptions& options);

    bool hasNotifiedCaller() { return inner_callbacks_ == nullptr; }

    // Event::DeferredDeletable
    // The wrapper is being deleted - cancel all alarms.
    void deleteIsPending() override { next_attempt_timer_.reset(); }

    // This holds state for a single connection attempt to a specific pool.
    class ConnectionAttemptCallbacks : public ConnectionPool::Callbacks,
                                       public LinkedObject<ConnectionAttemptCallbacks>,
                                       public Event::DeferredDeletable {
    public:
      ConnectionAttemptCallbacks(WrapperCallbacks& parent, ConnectionPool::Instance& pool);
      ~ConnectionAttemptCallbacks() override;
      void deleteIsPending() override {}

      StreamCreationResult newStream();

      // ConnectionPool::Callbacks
      void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason,
                         Upstream::HostDescriptionConstSharedPtr host) override;
      void onPoolReady(RequestEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host,
                       StreamInfo::StreamInfo& info,
                       absl::optional<Http::Protocol> protocol) override;

      ConnectionPool::Instance& pool() { return pool_; }

      void cancel(Envoy::ConnectionPool::CancelPolicy cancel_policy);

    private:
      // A pointer back up to the parent.
      WrapperCallbacks& parent_;
      ConnectionPool::Instance& pool_;
      // The handle to cancel this connection attempt.
      // This is owned by the pool which created it.
      Cancellable* cancellable_{nullptr};
    };
    using ConnectionAttemptCallbacksPtr = std::unique_ptr<ConnectionAttemptCallbacks>;

    // ConnectionPool::Cancellable
    void cancel(Envoy::ConnectionPool::CancelPolicy cancel_policy) override;

    // Attempt to create a new stream for pool.
    StreamCreationResult newStream(ConnectionPool::Instance& pool);

    // Called on pool failure or timeout to kick off another connection attempt.
    // Returns the StreamCreationResult if there is a failover pool and a
    // connection has been attempted, an empty optional otherwise.
    absl::optional<StreamCreationResult> tryAnotherConnection();

    // This timer is registered when an initial HTTP/3 attempt is started.
    // The timeout for TCP failover and HTTP/3 happy eyeballs are the same, so
    // when this timer fires it's possible that two additional connections will
    // be kicked off.
    void onNextAttemptTimer();

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

    // Called by onConnectionAttemptFailed and on grid deletion destruction to let wrapper
    // callback subscribers know the connect attempt failed.
    void signalFailureAndDeleteSelf(ConnectionPool::PoolFailureReason reason,
                                    absl::string_view transport_failure_reason,
                                    Upstream::HostDescriptionConstSharedPtr host);

    // Called if the initial HTTP/3 connection fails.
    // Returns true if an HTTP/3 happy eyeballs attempt can be kicked off
    // (runtime guard is on, IPv6 and IPv6 addresses are present, happy eyeballs
    // has not been tried yet for this wrapper, grid is not in shutdown).
    bool shouldAttemptSecondHttp3Connection();
    // This kicks off an HTTP/3 happy eyeballs attempt, connecting to the second
    // address in the host's address list.
    ConnectivityGrid::StreamCreationResult attemptSecondHttp3Connection();

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
    // Checks if http2 has been attempted.
    bool has_attempted_http2_ = false;
    // Checks if "happy eyeballs" has been done for HTTP/3. This largely makes
    // sure that if we kick off a secondary attempt due to timeout we don't kick
    // off another one if the original HTTP/3 connection explicitly fails.
    bool has_tried_http3_alternate_address_ = false;
    // True if the HTTP/3 attempt failed.
    bool http3_attempt_failed_{};
    // True if the TCP attempt succeeded.
    bool tcp_attempt_succeeded_{};
    // Latch the passed-in stream options.
    const Instance::StreamOptions stream_options_{};
    absl::optional<ConnectionPool::PoolFailureReason> prev_tcp_pool_failure_reason_;
    std::string prev_tcp_pool_transport_failure_reason_;
  };
  using WrapperCallbacksPtr = std::unique_ptr<WrapperCallbacks>;

  ConnectivityGrid(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                   Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                   Upstream::ClusterConnectivityState& state, TimeSource& time_source,
                   HttpServerPropertiesCacheSharedPtr alternate_protocols,
                   ConnectivityOptions connectivity_options, Quic::QuicStatNames& quic_stat_names,
                   Stats::Scope& scope, Http::PersistentQuicInfo& quic_info,
                   OptRef<Quic::EnvoyQuicNetworkObserverRegistry> network_observer_registry);
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

  // Returns the specified pool, which will be created if necessary
  ConnectionPool::Instance* getOrCreateHttp3Pool();
  ConnectionPool::Instance* getOrCreateHttp2Pool();
  ConnectionPool::Instance* getOrCreateHttp3AlternativePool();

  // True if this pool is the "happy eyeballs" attempt, and should use the
  // secondary address family.
  virtual ConnectionPool::InstancePtr createHttp3Pool(bool attempt_alternate_address);
  virtual ConnectionPool::InstancePtr createHttp2Pool();

  // This batch of member variables are latched objects required for pool creation.
  Event::Dispatcher& dispatcher_;
  Random::RandomGenerator& random_generator_;
  Upstream::HostConstSharedPtr host_;
  const Network::ConnectionSocket::OptionsSharedPtr options_;
  const Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  Upstream::ClusterConnectivityState& state_;
  std::chrono::milliseconds next_attempt_duration_;
  TimeSource& time_source_;
  HttpServerPropertiesCacheSharedPtr alternate_protocols_;

  // Tracks the callbacks to be called on drain completion.
  std::list<Instance::IdleCb> idle_callbacks_;

  // The connection pools to use to create new streams
  ConnectionPool::InstancePtr http3_pool_;
  // This is the pool used for the HTTP/3 "happy eyeballs" attempt. If it is
  // created it will have the opposite address family from http3_pool_ above.
  ConnectionPool::InstancePtr http3_alternate_pool_;
  ConnectionPool::InstancePtr http2_pool_;
  // A convenience vector to allow taking actions on all pools.
  absl::InlinedVector<ConnectionPool::Instance*, 2> pools_;

  // Wrapped callbacks are stashed in the wrapped_callbacks_ for ownership.
  std::list<WrapperCallbacksPtr> wrapped_callbacks_;

  Quic::QuicStatNames& quic_stat_names_;

  Stats::Scope& scope_;

  // The origin for this pool.
  // Note the host name here is based off of the host name used for SNI, which
  // may be from the cluster config, or the request headers for auto-sni.
  HttpServerPropertiesCache::Origin origin_;

  Http::PersistentQuicInfo& quic_info_;
  Upstream::ResourcePriority priority_;

  // True iff this pool is draining. No new streams or connections should be created
  // in this state.
  bool draining_{false};

  // True iff under the stack of the destructor, to avoid calling drain
  // callbacks on deletion.
  bool destroying_{};

  // True iff this pool is being deferred deleted.
  bool deferred_deleting_{};

  OptRef<Quic::EnvoyQuicNetworkObserverRegistry> network_observer_registry_;
};

} // namespace Http
} // namespace Envoy
