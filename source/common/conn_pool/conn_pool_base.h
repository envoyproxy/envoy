#pragma once

#include "envoy/common/conn_pool.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/stats/timespan.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/dump_state_utils.h"
#include "source/common/common/linked_object.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace ConnectionPool {

class ConnPoolImplBase;

// A placeholder struct for whatever data a given connection pool needs to
// successfully attach and upstream connection to a downstream connection.
struct AttachContext {
  // Add a virtual destructor to allow for the dynamic_cast ASSERT in typedContext.
  virtual ~AttachContext() = default;
};

// ActiveClient provides a base class for connection pool clients that handles connection timings
// as well as managing the connection timeout.
class ActiveClient : public LinkedObject<ActiveClient>,
                     public Network::ConnectionCallbacks,
                     public Event::DeferredDeletable,
                     protected Logger::Loggable<Logger::Id::pool> {
public:
  ActiveClient(ConnPoolImplBase& parent, uint32_t lifetime_stream_limit,
               uint32_t concurrent_stream_limit);
  ~ActiveClient() override;

  virtual void releaseResources() { releaseResourcesBase(); }
  void releaseResourcesBase();

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Called if the connection does not complete within the cluster's connectTimeout()
  void onConnectTimeout();

  // Returns the concurrent stream limit, accounting for if the total stream limit
  // is less than the concurrent stream limit.
  uint32_t effectiveConcurrentStreamLimit() const {
    return std::min(remaining_streams_, concurrent_stream_limit_);
  }

  // Returns the application protocol, or absl::nullopt for TCP.
  virtual absl::optional<Http::Protocol> protocol() const PURE;

  int64_t currentUnusedCapacity() const {
    int64_t remaining_concurrent_streams =
        static_cast<int64_t>(concurrent_stream_limit_) - numActiveStreams();

    return std::min<int64_t>(remaining_streams_, remaining_concurrent_streams);
  }

  // Closes the underlying connection.
  virtual void close() PURE;
  // Returns the ID of the underlying connection.
  virtual uint64_t id() const PURE;
  // Returns true if this closed with an incomplete stream, for stats tracking/ purposes.
  virtual bool closingWithIncompleteStream() const PURE;
  // Returns the number of active streams on this connection.
  virtual uint32_t numActiveStreams() const PURE;

  // This function is called onStreamClosed to see if there was a negative delta
  // and (if necessary) update associated bookkeeping.
  // HTTP/1 and TCP pools can not have negative delta so the default implementation simply returns
  // false. The HTTP/2 connection pool can have this state, so overrides this function.
  virtual bool hadNegativeDeltaOnStreamClosed() { return false; }

  enum class State {
    CONNECTING, // Connection is not yet established.
    READY,      // Additional streams may be immediately dispatched to this connection.
    BUSY,       // Connection is at its concurrent stream limit.
    DRAINING,   // No more streams can be dispatched to this connection, and it will be closed
    // when all streams complete.
    CLOSED // Connection is closed and object is queued for destruction.
  };

  State state() const { return state_; }

  void setState(State state) {
    // If the client is transitioning to draining, update the remaining
    // streams and pool and cluster capacity.
    if (state == State::DRAINING) {
      drain();
    }
    state_ = state;
  }

  // Sets the remaining streams to 0, and updates pool and cluster capacity.
  virtual void drain();

  ConnPoolImplBase& parent_;
  uint32_t remaining_streams_;
  uint32_t concurrent_stream_limit_;
  Upstream::HostDescriptionConstSharedPtr real_host_description_;
  Stats::TimespanPtr conn_connect_ms_;
  Stats::TimespanPtr conn_length_;
  Event::TimerPtr connect_timer_;
  bool resources_released_{false};
  bool timed_out_{false};

private:
  State state_{State::CONNECTING};
};

// PendingStream is the base class tracking streams for which a connection has been created but not
// yet established.
class PendingStream : public LinkedObject<PendingStream>, public ConnectionPool::Cancellable {
public:
  PendingStream(ConnPoolImplBase& parent);
  ~PendingStream() override;

  // ConnectionPool::Cancellable
  void cancel(Envoy::ConnectionPool::CancelPolicy policy) override;

  // The context here returns a pointer to whatever context is provided with newStream(),
  // which will be passed back to the parent in onPoolReady or onPoolFailure.
  virtual AttachContext& context() PURE;

  ConnPoolImplBase& parent_;
};

using PendingStreamPtr = std::unique_ptr<PendingStream>;

using ActiveClientPtr = std::unique_ptr<ActiveClient>;

// Base class that handles stream queueing logic shared between connection pool implementations.
class ConnPoolImplBase : protected Logger::Loggable<Logger::Id::pool> {
public:
  ConnPoolImplBase(Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                   Event::Dispatcher& dispatcher,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                   Upstream::ClusterConnectivityState& state);
  virtual ~ConnPoolImplBase();

  void deleteIsPendingImpl();

  // A helper function to get the specific context type from the base class context.
  template <class T> T& typedContext(AttachContext& context) {
    ASSERT(dynamic_cast<T*>(&context) != nullptr);
    return *static_cast<T*>(&context);
  }

  // Determines if prefetching is warranted based on the number of streams in
  // use, pending streams, anticipated and/or currently unused capacity, and
  // preconnect configuration.
  //
  // If anticipate_incoming_stream is true this assumes a call to newStream is
  // pending, which is true for global preconnect.
  static bool shouldConnect(size_t pending_streams, size_t active_streams,
                            int64_t connecting_and_connected_capacity, float preconnect_ratio,
                            bool anticipate_incoming_stream = false);

  void addIdleCallbackImpl(Instance::IdleCb cb);
  void startDrainImpl();
  void drainConnectionsImpl();

  // Closes and destroys all connections. This must be called in the destructor of
  // derived classes because the derived ActiveClient will downcast parent_ to a more
  // specific type of ConnPoolImplBase, but if the more specific part is already destructed
  // (due to bottom-up destructor ordering in c++) that access will be invalid.
  void destructAllConnections();

  // Returns a new instance of ActiveClient.
  virtual ActiveClientPtr instantiateActiveClient() PURE;

  // Gets a pointer to the list that currently owns this client.
  std::list<ActiveClientPtr>& owningList(ActiveClient::State state);

  // Removes the PendingStream from the list of streams. Called when the PendingStream is
  // cancelled, e.g. when the stream is reset before a connection has been established.
  void onPendingStreamCancel(PendingStream& stream, Envoy::ConnectionPool::CancelPolicy policy);

  // Fails all pending streams, calling onPoolFailure on the associated callbacks.
  void purgePendingStreams(const Upstream::HostDescriptionConstSharedPtr& host_description,
                           absl::string_view failure_reason,
                           ConnectionPool::PoolFailureReason pool_failure_reason);

  // Closes any idle connections as this pool is drained.
  void closeIdleConnectionsForDrainingPool();

  // Changes the state_ of an ActiveClient and moves to the appropriate list.
  void transitionActiveClientState(ActiveClient& client, ActiveClient::State new_state);

  void onConnectionEvent(ActiveClient& client, absl::string_view failure_reason,
                         Network::ConnectionEvent event);

  // Returns true if the pool is idle.
  bool isIdleImpl() const;

  // See if the pool has gone idle. If we're draining, this will also close idle connections.
  void checkForIdleAndCloseIdleConnsIfDraining();

  void scheduleOnUpstreamReady();
  ConnectionPool::Cancellable* newStream(AttachContext& context);
  // Called if this pool is likely to be picked soon, to determine if it's worth preconnecting.
  bool maybePreconnect(float global_preconnect_ratio);

  virtual ConnectionPool::Cancellable* newPendingStream(AttachContext& context) PURE;

  virtual void attachStreamToClient(Envoy::ConnectionPool::ActiveClient& client,
                                    AttachContext& context);

  virtual void onPoolFailure(const Upstream::HostDescriptionConstSharedPtr& host_description,
                             absl::string_view failure_reason,
                             ConnectionPool::PoolFailureReason pool_failure_reason,
                             AttachContext& context) PURE;
  virtual void onPoolReady(ActiveClient& client, AttachContext& context) PURE;
  // Called by derived classes any time a stream is completed or destroyed for any reason.
  void onStreamClosed(Envoy::ConnectionPool::ActiveClient& client, bool delay_attaching_stream);

  const Upstream::HostConstSharedPtr& host() const { return host_; }
  Event::Dispatcher& dispatcher() { return dispatcher_; }
  Upstream::ResourcePriority priority() const { return priority_; }
  const Network::ConnectionSocket::OptionsSharedPtr& socketOptions() { return socket_options_; }
  const Network::TransportSocketOptionsConstSharedPtr& transportSocketOptions() {
    return transport_socket_options_;
  }
  bool hasPendingStreams() const { return !pending_streams_.empty(); }

  void decrClusterStreamCapacity(uint32_t delta) {
    state_.decrConnectingAndConnectedStreamCapacity(delta);
  }
  void dumpState(std::ostream& os, int indent_level = 0) const {
    const char* spaces = spacesForLevel(indent_level);
    os << spaces << "ConnPoolImplBase " << this << DUMP_MEMBER(ready_clients_.size())
       << DUMP_MEMBER(busy_clients_.size()) << DUMP_MEMBER(connecting_clients_.size())
       << DUMP_MEMBER(connecting_stream_capacity_) << DUMP_MEMBER(num_active_streams_)
       << DUMP_MEMBER(pending_streams_.size())
       << " per upstream preconnect ratio: " << perUpstreamPreconnectRatio();
  }

  friend std::ostream& operator<<(std::ostream& os, const ConnPoolImplBase& s) {
    s.dumpState(os);
    return os;
  }
  Upstream::ClusterConnectivityState& state() { return state_; }

  void decrConnectingAndConnectedStreamCapacity(uint32_t delta) {
    state_.decrConnectingAndConnectedStreamCapacity(delta);
    ASSERT(connecting_stream_capacity_ >= delta);
    connecting_stream_capacity_ -= delta;
  }

protected:
  virtual void onConnected(Envoy::ConnectionPool::ActiveClient&) {}

  enum class ConnectionResult {
    FailedToCreateConnection,
    CreatedNewConnection,
    ShouldNotConnect,
    NoConnectionRateLimited,
    CreatedButRateLimited,
  };

  // Creates up to 3 connections, based on the preconnect ratio.
  // Returns the ConnectionResult of the last attempt.
  ConnectionResult tryCreateNewConnections();

  // Creates a new connection if there is sufficient demand, it is allowed by resourceManager, or
  // to avoid starving this pool.
  // Demand is determined either by perUpstreamPreconnectRatio() or global_preconnect_ratio
  // if this is called by maybePreconnect()
  ConnectionResult tryCreateNewConnection(float global_preconnect_ratio = 0);

  // A helper function which determines if a canceled pending connection should
  // be closed as excess or not.
  bool connectingConnectionIsExcess() const;

  // A helper function which determines if a new incoming stream should trigger
  // connection preconnect.
  bool shouldCreateNewConnection(float global_preconnect_ratio) const;

  float perUpstreamPreconnectRatio() const;

  ConnectionPool::Cancellable*
  addPendingStream(Envoy::ConnectionPool::PendingStreamPtr&& pending_stream) {
    LinkedList::moveIntoList(std::move(pending_stream), pending_streams_);
    state_.incrPendingStreams(1);
    return pending_streams_.front().get();
  }

  bool hasActiveStreams() const { return num_active_streams_ > 0; }

  void incrConnectingAndConnectedStreamCapacity(uint32_t delta) {
    state_.incrConnectingAndConnectedStreamCapacity(delta);
    connecting_stream_capacity_ += delta;
  }

  Upstream::ClusterConnectivityState& state_;

  const Upstream::HostConstSharedPtr host_;
  const Upstream::ResourcePriority priority_;

  Event::Dispatcher& dispatcher_;
  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;
  const Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;

  std::list<Instance::IdleCb> idle_callbacks_;

  // When calling purgePendingStreams, this list will be used to hold the streams we are about
  // to purge. We need this if one cancelled streams cancels a different pending stream
  std::list<PendingStreamPtr> pending_streams_to_purge_;

  // Clients that are ready to handle additional streams.
  // All entries are in state READY.
  std::list<ActiveClientPtr> ready_clients_;

  // Clients that are not ready to handle additional streams due to being BUSY or DRAINING.
  std::list<ActiveClientPtr> busy_clients_;

  // Clients that are not ready to handle additional streams because they are CONNECTING.
  std::list<ActiveClientPtr> connecting_clients_;

  // The number of streams that can be immediately dispatched
  // if all CONNECTING connections become connected.
  uint32_t connecting_stream_capacity_{0};

private:
  std::list<PendingStreamPtr> pending_streams_;

  // The number of streams currently attached to clients.
  uint32_t num_active_streams_{0};

  // Whether the connection pool is currently in the process of closing
  // all connections so that it can be gracefully deleted.
  bool is_draining_{false};

  // True iff this object is in the deferred delete list.
  bool deferred_deleting_{false};

  void onUpstreamReady();
  Event::SchedulableCallbackPtr upstream_ready_cb_;
};

} // namespace ConnectionPool
} // namespace Envoy
