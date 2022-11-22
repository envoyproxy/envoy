#include "source/common/conn_pool/conn_pool_base.h"

#include "source/common/common/assert.h"
#include "source/common/common/debug_recursion_checker.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/timespan_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace ConnectionPool {
namespace {
[[maybe_unused]] ssize_t connectingCapacity(const std::list<ActiveClientPtr>& connecting_clients) {
  ssize_t ret = 0;
  for (const auto& client : connecting_clients) {
    ret += client->currentUnusedCapacity();
  }
  return ret;
}
} // namespace

ConnPoolImplBase::ConnPoolImplBase(
    Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    Upstream::ClusterConnectivityState& state)
    : state_(state), host_(host), priority_(priority), dispatcher_(dispatcher),
      socket_options_(options), transport_socket_options_(transport_socket_options),
      upstream_ready_cb_(dispatcher_.createSchedulableCallback([this]() { onUpstreamReady(); })) {}

ConnPoolImplBase::~ConnPoolImplBase() {
  ASSERT(isIdleImpl());
  ASSERT(connecting_stream_capacity_ == 0);
}

void ConnPoolImplBase::deleteIsPendingImpl() {
  deferred_deleting_ = true;
  ASSERT(isIdleImpl());
  ASSERT(connecting_stream_capacity_ == 0);
}

void ConnPoolImplBase::destructAllConnections() {
  for (auto* list : {&ready_clients_, &busy_clients_, &connecting_clients_, &early_data_clients_}) {
    while (!list->empty()) {
      list->front()->close();
    }
  }

  // Make sure all clients are destroyed before we are destroyed.
  dispatcher_.clearDeferredDeleteList();
}

bool ConnPoolImplBase::shouldConnect(size_t pending_streams, size_t active_streams,
                                     int64_t connecting_and_connected_capacity,
                                     float preconnect_ratio, bool anticipate_incoming_stream) {
  // This is set to true any time global preconnect is being calculated.
  // ClusterManagerImpl::maybePreconnect is called directly before a stream is created, so the
  // stream must be anticipated.
  //
  // Also without this, we would never pre-establish a connection as the first
  // connection in a pool because pending/active streams could both be 0.
  int anticipated_streams = anticipate_incoming_stream ? 1 : 0;

  // The number of streams we want to be provisioned for is the number of
  // pending, active, and anticipated streams times the preconnect ratio.
  // The number of streams we are (theoretically) provisioned for is the
  // connecting stream capacity plus the number of active streams.
  //
  // If preconnect ratio is not set, it defaults to 1, and this simplifies to the
  // legacy value of pending_streams_.size() > connecting_stream_capacity_
  return (pending_streams + active_streams + anticipated_streams) * preconnect_ratio >
         connecting_and_connected_capacity + active_streams;
}

bool ConnPoolImplBase::shouldCreateNewConnection(float global_preconnect_ratio) const {
  // If the host is not healthy, don't make it do extra work, especially as
  // upstream selection logic may result in bypassing this upstream entirely.
  // If an Envoy user wants preconnecting for degraded upstreams this could be
  // added later via extending the preconnect config.
  if (host_->coarseHealth() != Upstream::Host::Health::Healthy) {
    return pending_streams_.size() > connecting_stream_capacity_;
  }

  // Determine if we are trying to prefetch for global preconnect or local preconnect.
  if (global_preconnect_ratio != 0) {
    // If global preconnecting is on, and this connection is within the global
    // preconnect limit, preconnect.
    // For global preconnect, we anticipate an incoming stream to this pool, since it is
    // prefetching for the next upcoming stream, which will likely be assigned to this pool.
    // We may eventually want to track preconnect_attempts to allow more preconnecting for
    // heavily weighted upstreams or sticky picks.
    return shouldConnect(pending_streams_.size(), num_active_streams_, connecting_stream_capacity_,
                         global_preconnect_ratio, true);
  } else {
    // Ensure this local pool has adequate connections for the given load.
    //
    // Local preconnect does not need to anticipate a stream. It is called as
    // new streams are established or torn down and simply attempts to maintain
    // the correct ratio of streams and anticipated capacity.
    return shouldConnect(pending_streams_.size(), num_active_streams_, connecting_stream_capacity_,
                         perUpstreamPreconnectRatio());
  }
}

float ConnPoolImplBase::perUpstreamPreconnectRatio() const {
  return host_->cluster().perUpstreamPreconnectRatio();
}

ConnPoolImplBase::ConnectionResult ConnPoolImplBase::tryCreateNewConnections() {
  ASSERT(!is_draining_for_deletion_);
  ConnPoolImplBase::ConnectionResult result;
  // Somewhat arbitrarily cap the number of connections preconnected due to new
  // incoming connections. The preconnect ratio is capped at 3, so in steady
  // state, no more than 3 connections should be preconnected. If hosts go
  // unhealthy, and connections are not immediately preconnected, it could be that
  // many connections are desired when the host becomes healthy again, but
  // overwhelming it with connections is not desirable.
  for (int i = 0; i < 3; ++i) {
    result = tryCreateNewConnection();
    if (result != ConnectionResult::CreatedNewConnection) {
      break;
    }
  }
  return result;
}

ConnPoolImplBase::ConnectionResult
ConnPoolImplBase::tryCreateNewConnection(float global_preconnect_ratio) {
  // There are already enough Connecting connections for the number of queued streams.
  if (!shouldCreateNewConnection(global_preconnect_ratio)) {
    ENVOY_LOG(trace, "not creating a new connection, shouldCreateNewConnection returned false.");
    return ConnectionResult::ShouldNotConnect;
  }

  const bool can_create_connection = host_->canCreateConnection(priority_);

  if (!can_create_connection) {
    host_->cluster().trafficStats().upstream_cx_overflow_.inc();
  }
  // If we are at the connection circuit-breaker limit due to other upstreams having
  // too many open connections, and this upstream has no connections, always create one, to
  // prevent pending streams being queued to this upstream with no way to be processed.
  if (can_create_connection || (ready_clients_.empty() && busy_clients_.empty() &&
                                connecting_clients_.empty() && early_data_clients_.empty())) {
    ENVOY_LOG(debug, "creating a new connection (connecting={})", connecting_clients_.size());
    ActiveClientPtr client = instantiateActiveClient();
    if (client.get() == nullptr) {
      ENVOY_LOG(trace, "connection creation failed");
      return ConnectionResult::FailedToCreateConnection;
    }
    ASSERT(client->state() == ActiveClient::State::Connecting);
    ASSERT(std::numeric_limits<uint64_t>::max() - connecting_stream_capacity_ >=
           static_cast<uint64_t>(client->currentUnusedCapacity()));
    ASSERT(client->real_host_description_);
    // Increase the connecting capacity to reflect the streams this connection can serve.
    incrConnectingAndConnectedStreamCapacity(client->currentUnusedCapacity(), *client);
    LinkedList::moveIntoList(std::move(client), owningList(client->state()));
    return can_create_connection ? ConnectionResult::CreatedNewConnection
                                 : ConnectionResult::CreatedButRateLimited;
  } else {
    ENVOY_LOG(trace, "not creating a new connection: connection constrained");
    return ConnectionResult::NoConnectionRateLimited;
  }
}

void ConnPoolImplBase::attachStreamToClient(Envoy::ConnectionPool::ActiveClient& client,
                                            AttachContext& context) {
  ASSERT(client.readyForStream());

  if (client.state() == Envoy::ConnectionPool::ActiveClient::State::ReadyForEarlyData) {
    host_->cluster().trafficStats().upstream_rq_0rtt_.inc();
  }

  if (enforceMaxRequests() && !host_->cluster().resourceManager(priority_).requests().canCreate()) {
    ENVOY_LOG(debug, "max streams overflow");
    onPoolFailure(client.real_host_description_, absl::string_view(),
                  ConnectionPool::PoolFailureReason::Overflow, context);
    host_->cluster().trafficStats().upstream_rq_pending_overflow_.inc();
    return;
  }
  ENVOY_CONN_LOG(debug, "creating stream", client);

  // Latch capacity before updating remaining streams.
  uint64_t capacity = client.currentUnusedCapacity();
  client.remaining_streams_--;
  if (client.remaining_streams_ == 0) {
    ENVOY_CONN_LOG(debug, "maximum streams per connection, start draining", client);
    host_->cluster().trafficStats().upstream_cx_max_requests_.inc();
    transitionActiveClientState(client, Envoy::ConnectionPool::ActiveClient::State::Draining);
  } else if (capacity == 1) {
    // As soon as the new stream is created, the client will be maxed out.
    transitionActiveClientState(client, Envoy::ConnectionPool::ActiveClient::State::Busy);
  }

  // Decrement the capacity, as there's one less stream available for serving.
  // For HTTP/3, the capacity is updated in newStreamEncoder.
  if (trackStreamCapacity()) {
    decrConnectingAndConnectedStreamCapacity(1, client);
  }
  // Track the new active stream.
  state_.incrActiveStreams(1);
  num_active_streams_++;
  host_->stats().rq_total_.inc();
  host_->stats().rq_active_.inc();
  host_->cluster().trafficStats().upstream_rq_total_.inc();
  host_->cluster().trafficStats().upstream_rq_active_.inc();
  host_->cluster().resourceManager(priority_).requests().inc();

  onPoolReady(client, context);
}

void ConnPoolImplBase::onStreamClosed(Envoy::ConnectionPool::ActiveClient& client,
                                      bool delay_attaching_stream) {
  ENVOY_CONN_LOG(debug, "destroying stream: {} remaining", client, client.numActiveStreams());
  ASSERT(num_active_streams_ > 0);
  state_.decrActiveStreams(1);
  num_active_streams_--;
  host_->stats().rq_active_.dec();
  host_->cluster().trafficStats().upstream_rq_active_.dec();
  host_->cluster().resourceManager(priority_).requests().dec();
  // We don't update the capacity for HTTP/3 as the stream count should only
  // increase when a MAX_STREAMS frame is received.
  if (trackStreamCapacity()) {
    // If the effective client capacity was limited by concurrency, increase connecting capacity.
    bool limited_by_concurrency =
        client.remaining_streams_ > client.concurrent_stream_limit_ - client.numActiveStreams() - 1;
    // The capacity calculated by concurrency could be negative if a SETTINGS frame lowered the
    // number of allowed streams. In this case, effective client capacity was still limited by
    // concurrency, compare client.concurrent_stream_limit_ and client.numActiveStreams() directly
    // to avoid overflow.
    bool negative_capacity = client.concurrent_stream_limit_ < client.numActiveStreams() + 1;
    if (negative_capacity || limited_by_concurrency) {
      incrConnectingAndConnectedStreamCapacity(1, client);
    }
  }
  if (client.state() == ActiveClient::State::Draining && client.numActiveStreams() == 0) {
    // Close out the draining client if we no longer have active streams.
    client.close();
  } else if (client.state() == ActiveClient::State::Busy && client.currentUnusedCapacity() > 0) {
    if (!client.hasHandshakeCompleted()) {
      transitionActiveClientState(client, ActiveClient::State::ReadyForEarlyData);
      if (!delay_attaching_stream) {
        onUpstreamReadyForEarlyData(client);
      }
    } else {
      transitionActiveClientState(client, ActiveClient::State::Ready);
      if (!delay_attaching_stream) {
        onUpstreamReady();
      }
    }
  }
}

ConnectionPool::Cancellable* ConnPoolImplBase::newStreamImpl(AttachContext& context,
                                                             bool can_send_early_data) {
  ASSERT(!is_draining_for_deletion_);
  ASSERT(!deferred_deleting_);

  ASSERT(static_cast<ssize_t>(connecting_stream_capacity_) ==
         connectingCapacity(connecting_clients_) +
             connectingCapacity(early_data_clients_)); // O(n) debug check.
  if (!ready_clients_.empty()) {
    ActiveClient& client = *ready_clients_.front();
    ENVOY_CONN_LOG(debug, "using existing fully connected connection", client);
    attachStreamToClient(client, context);
    // Even if there's a ready client, we may want to preconnect to handle the next incoming stream.
    tryCreateNewConnections();
    return nullptr;
  }

  if (can_send_early_data && !early_data_clients_.empty()) {
    ActiveClient& client = *early_data_clients_.front();
    ENVOY_CONN_LOG(debug, "using existing early data ready connection", client);
    attachStreamToClient(client, context);
    // Even if there's an available client, we may want to preconnect to handle the next
    // incoming stream.
    tryCreateNewConnections();
    return nullptr;
  }

  if (!host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
    ENVOY_LOG(debug, "max pending streams overflow");
    onPoolFailure(nullptr, absl::string_view(), ConnectionPool::PoolFailureReason::Overflow,
                  context);
    host_->cluster().trafficStats().upstream_rq_pending_overflow_.inc();
    return nullptr;
  }

  ConnectionPool::Cancellable* pending = newPendingStream(context, can_send_early_data);
  ENVOY_LOG(debug, "trying to create new connection");
  ENVOY_LOG(trace, fmt::format("{}", *this));

  auto old_capacity = connecting_stream_capacity_;
  // This must come after newPendingStream() because this function uses the
  // length of pending_streams_ to determine if a new connection is needed.
  const ConnectionResult result = tryCreateNewConnections();
  // If there is not enough connecting capacity, the only reason to not
  // increase capacity is if the connection limits are exceeded.
  ENVOY_BUG(pending_streams_.size() <= connecting_stream_capacity_ ||
                connecting_stream_capacity_ > old_capacity ||
                (result == ConnectionResult::NoConnectionRateLimited ||
                 result == ConnectionResult::FailedToCreateConnection),
            fmt::format("Failed to create expected connection: {}", *this));
  if (result == ConnectionResult::FailedToCreateConnection) {
    // This currently only happens for HTTP/3 if secrets aren't yet loaded.
    // Trigger connection failure.
    pending->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
    onPoolFailure(nullptr, absl::string_view(),
                  ConnectionPool::PoolFailureReason::LocalConnectionFailure, context);
    return nullptr;
  }
  return pending;
}

bool ConnPoolImplBase::maybePreconnectImpl(float global_preconnect_ratio) {
  ASSERT(!deferred_deleting_);
  return tryCreateNewConnection(global_preconnect_ratio) == ConnectionResult::CreatedNewConnection;
}

void ConnPoolImplBase::scheduleOnUpstreamReady() {
  upstream_ready_cb_->scheduleCallbackCurrentIteration();
}

void ConnPoolImplBase::onUpstreamReady() {
  while (!pending_streams_.empty() && !ready_clients_.empty()) {
    ActiveClientPtr& client = ready_clients_.front();
    ENVOY_CONN_LOG(debug, "attaching to next stream", *client);
    // Pending streams are pushed onto the front, so pull from the back.
    attachStreamToClient(*client, pending_streams_.back()->context());
    state_.decrPendingStreams(1);
    pending_streams_.pop_back();
  }
  if (!pending_streams_.empty()) {
    tryCreateNewConnections();
  }
}

std::list<ActiveClientPtr>& ConnPoolImplBase::owningList(ActiveClient::State state) {
  switch (state) {
  case ActiveClient::State::Connecting:
    return connecting_clients_;
  case ActiveClient::State::ReadyForEarlyData:
    return early_data_clients_;
  case ActiveClient::State::Ready:
    return ready_clients_;
  case ActiveClient::State::Busy:
  case ActiveClient::State::Draining:
    return busy_clients_;
  case ActiveClient::State::Closed:
    break; // Fall through to PANIC.
  }
  PANIC("unexpected");
}

void ConnPoolImplBase::transitionActiveClientState(ActiveClient& client,
                                                   ActiveClient::State new_state) {
  auto& old_list = owningList(client.state());
  auto& new_list = owningList(new_state);
  client.setState(new_state);

  // old_list and new_list can be equal when transitioning from Busy to Draining.
  //
  // The documentation for list.splice() (which is what moveBetweenLists() calls) is
  // unclear whether it is allowed for src and dst to be the same, so check here
  // since it is a no-op anyways.
  if (&old_list != &new_list) {
    client.moveBetweenLists(old_list, new_list);
  }
}

void ConnPoolImplBase::addIdleCallbackImpl(Instance::IdleCb cb) { idle_callbacks_.push_back(cb); }

void ConnPoolImplBase::closeIdleConnectionsForDrainingPool() {
  Common::AutoDebugRecursionChecker assert_not_in(recursion_checker_);

  // Create a separate list of elements to close to avoid mutate-while-iterating problems.
  std::list<ActiveClient*> to_close;

  for (auto& client : ready_clients_) {
    if (client->numActiveStreams() == 0) {
      to_close.push_back(client.get());
    }
  }

  if (pending_streams_.empty()) {
    for (auto& client : connecting_clients_) {
      to_close.push_back(client.get());
    }
    for (auto& client : early_data_clients_) {
      if (client->numActiveStreams() == 0) {
        to_close.push_back(client.get());
      }
    }
  }

  for (auto& entry : to_close) {
    ENVOY_LOG_EVENT(debug, "closing_idle_client", "closing idle client {} for cluster {}",
                    entry->id(), host_->cluster().name());
    entry->close();
  }
}

void ConnPoolImplBase::drainClients(std::list<ActiveClientPtr>& clients) {
  while (!clients.empty()) {
    ASSERT(clients.front()->numActiveStreams() > 0u);
    ENVOY_LOG_EVENT(
        debug, "draining_non_idle_client", "draining {} client {} for cluster {}",
        (clients.front()->state() == ActiveClient::State::Ready ? "ready" : "early data"),
        clients.front()->id(), host_->cluster().name());
    transitionActiveClientState(*clients.front(), ActiveClient::State::Draining);
  }
}

void ConnPoolImplBase::drainConnectionsImpl(DrainBehavior drain_behavior) {
  if (drain_behavior == Envoy::ConnectionPool::DrainBehavior::DrainAndDelete) {
    is_draining_for_deletion_ = true;
    checkForIdleAndCloseIdleConnsIfDraining();
    return;
  }
  closeIdleConnectionsForDrainingPool();
  // closeIdleConnectionsForDrainingPool() closes all connections in ready_clients_ with no active
  // streams and if no pending streams, all connections in early_data_clients_ with no active
  // streams as well, so all remaining entries in ready_clients_ are serving streams. Move them and
  // all entries in busy_clients_ to draining.
  if (pending_streams_.empty()) {
    // The remaining early data clients are non-idle.
    drainClients(early_data_clients_);
  }

  drainClients(ready_clients_);

  // Changing busy_clients_ to Draining does not move them between lists,
  // so use a for-loop since the list is not mutated.
  ASSERT(&owningList(ActiveClient::State::Draining) == &busy_clients_);
  for (auto& busy_client : busy_clients_) {
    if (busy_client->state() == ActiveClient::State::Draining) {
      continue;
    }
    ENVOY_LOG_EVENT(debug, "draining_busy_client", "draining busy client {} for cluster {}",
                    busy_client->id(), host_->cluster().name());
    transitionActiveClientState(*busy_client, ActiveClient::State::Draining);
  }
}

bool ConnPoolImplBase::isIdleImpl() const {
  return pending_streams_.empty() && ready_clients_.empty() && busy_clients_.empty() &&
         connecting_clients_.empty() && early_data_clients_.empty();
}

void ConnPoolImplBase::checkForIdleAndNotify() {
  if (isIdleImpl()) {
    ENVOY_LOG(debug, "invoking idle callbacks - is_draining_for_deletion_={}",
              is_draining_for_deletion_);
    for (const Instance::IdleCb& cb : idle_callbacks_) {
      cb();
    }
  }
}

void ConnPoolImplBase::checkForIdleAndCloseIdleConnsIfDraining() {
  if (is_draining_for_deletion_) {
    closeIdleConnectionsForDrainingPool();
  }

  checkForIdleAndNotify();
}

void ConnPoolImplBase::onConnectionEvent(ActiveClient& client, absl::string_view failure_reason,
                                         Network::ConnectionEvent event) {
  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
  case Network::ConnectionEvent::LocalClose: {
    if (client.connect_timer_) {
      ASSERT(!client.has_handshake_completed_);
      client.connect_timer_->disableTimer();
      client.connect_timer_.reset();
    }
    decrConnectingAndConnectedStreamCapacity(client.currentUnusedCapacity(), client);

    // Make sure that onStreamClosed won't double count.
    client.remaining_streams_ = 0;
    // The client died.
    ENVOY_CONN_LOG(debug, "client disconnected, failure reason: {}", client, failure_reason);

    Envoy::Upstream::reportUpstreamCxDestroy(host_, event);
    const bool incomplete_stream = client.closingWithIncompleteStream();
    if (incomplete_stream) {
      Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);
    }

    if (!client.hasHandshakeCompleted()) {
      client.has_handshake_completed_ = true;
      host_->cluster().trafficStats().upstream_cx_connect_fail_.inc();
      host_->stats().cx_connect_fail_.inc();

      onConnectFailed(client);
      // Purge pending streams only if this client doesn't contribute to the local connecting
      // stream capacity. In other words, the rest clients  would be able to handle all the
      // pending stream once they are connected.
      ConnectionPool::PoolFailureReason reason;
      if (client.timed_out_) {
        reason = ConnectionPool::PoolFailureReason::Timeout;
      } else if (event == Network::ConnectionEvent::RemoteClose) {
        reason = ConnectionPool::PoolFailureReason::RemoteConnectionFailure;
      } else {
        reason = ConnectionPool::PoolFailureReason::LocalConnectionFailure;
      }

      // Raw connect failures should never happen under normal circumstances. If we have an
      // upstream that is behaving badly, streams can get stuck here in the pending state. If we
      // see a connect failure, we purge all pending streams so that calling code can determine
      // what to do with the stream.
      // NOTE: We move the existing pending streams to a temporary list. This is done so that
      //       if retry logic submits a new stream to the pool, we don't fail it inline.
      purgePendingStreams(client.real_host_description_, failure_reason, reason);
      // See if we should preconnect based on active connections.
      if (!is_draining_for_deletion_) {
        tryCreateNewConnections();
      }
    }

    // We need to release our resourceManager() resources before checking below for
    // whether we can create a new connection. Normally this would happen when
    // client's destructor runs, but this object needs to be deferredDelete'd(), so
    // this forces part of its cleanup to happen now.
    client.releaseResources();

    // Again, since we know this object is going to be deferredDelete'd(), we take
    // this opportunity to disable and reset the connection duration timer so that
    // it doesn't trigger while on the deferred delete list. In theory it is safe
    // to handle the Closed state in onConnectionDurationTimeout, but we handle
    // it here for simplicity and safety anyway.
    if (client.connection_duration_timer_) {
      client.connection_duration_timer_->disableTimer();
      client.connection_duration_timer_.reset();
    }

    dispatcher_.deferredDelete(client.removeFromList(owningList(client.state())));

    // Check if the pool transitioned to idle state after removing closed client
    // from one of the client tracking lists.
    // There is no need to check if other connections are idle in a draining pool
    // because the pool will close all idle connection when it is starting to
    // drain.
    // Trying to close other connections here can lead to deep recursion when
    // a large number idle connections are closed at the start of pool drain.
    // See CdsIntegrationTest.CdsClusterDownWithLotsOfIdleConnections for an example.
    checkForIdleAndNotify();

    client.setState(ActiveClient::State::Closed);

    // If we have pending streams and we just lost a connection we should make a new one.
    if (!pending_streams_.empty()) {
      tryCreateNewConnections();
    }
    break;
  }
  case Network::ConnectionEvent::Connected: {
    ASSERT(client.connect_timer_ != nullptr && !client.has_handshake_completed_);
    client.connect_timer_->disableTimer();
    client.connect_timer_.reset();

    ASSERT(connecting_stream_capacity_ >= client.currentUnusedCapacity());
    connecting_stream_capacity_ -= client.currentUnusedCapacity();
    client.has_handshake_completed_ = true;
    client.conn_connect_ms_->complete();
    client.conn_connect_ms_.reset();
    if (client.state() == ActiveClient::State::Connecting ||
        client.state() == ActiveClient::State::ReadyForEarlyData) {
      transitionActiveClientState(client,
                                  (client.currentUnusedCapacity() > 0 ? ActiveClient::State::Ready
                                                                      : ActiveClient::State::Busy));
    }

    // Now that the active client is ready, set up a timer for max connection duration.
    const absl::optional<std::chrono::milliseconds> max_connection_duration =
        client.parent_.host()->cluster().maxConnectionDuration();
    if (max_connection_duration.has_value()) {
      client.connection_duration_timer_ = client.parent_.dispatcher().createTimer(
          [&client]() { client.onConnectionDurationTimeout(); });
      client.connection_duration_timer_->enableTimer(max_connection_duration.value());
    }

    // At this point, for the mixed ALPN pool, the client may be deleted. Do not
    // refer to client after this point.
    onConnected(client);
    if (client.readyForStream()) {
      onUpstreamReady();
    }
    checkForIdleAndCloseIdleConnsIfDraining();
    break;
  }
  case Network::ConnectionEvent::ConnectedZeroRtt: {
    ENVOY_CONN_LOG(debug, "0-RTT connected with capacity {}", client,
                   client.currentUnusedCapacity());
    // No need to update connecting capacity and connect_timer_ as the client is still connecting.
    ASSERT(client.state() == ActiveClient::State::Connecting);
    host()->cluster().trafficStats().upstream_cx_connect_with_0_rtt_.inc();
    transitionActiveClientState(client, (client.currentUnusedCapacity() > 0
                                             ? ActiveClient::State::ReadyForEarlyData
                                             : ActiveClient::State::Busy));
    break;
  }
  }
}

PendingStream::PendingStream(ConnPoolImplBase& parent, bool can_send_early_data)
    : parent_(parent), can_send_early_data_(can_send_early_data) {
  parent_.host()->cluster().trafficStats().upstream_rq_pending_total_.inc();
  parent_.host()->cluster().trafficStats().upstream_rq_pending_active_.inc();
  parent_.host()->cluster().resourceManager(parent_.priority()).pendingRequests().inc();
}

PendingStream::~PendingStream() {
  parent_.host()->cluster().trafficStats().upstream_rq_pending_active_.dec();
  parent_.host()->cluster().resourceManager(parent_.priority()).pendingRequests().dec();
}

void PendingStream::cancel(Envoy::ConnectionPool::CancelPolicy policy) {
  parent_.onPendingStreamCancel(*this, policy);
}

void ConnPoolImplBase::purgePendingStreams(
    const Upstream::HostDescriptionConstSharedPtr& host_description,
    absl::string_view failure_reason, ConnectionPool::PoolFailureReason reason) {
  // NOTE: We move the existing pending streams to a temporary list. This is done so that
  //       if retry logic submits a new stream to the pool, we don't fail it inline.
  state_.decrPendingStreams(pending_streams_.size());
  pending_streams_to_purge_ = std::move(pending_streams_);
  while (!pending_streams_to_purge_.empty()) {
    PendingStreamPtr stream =
        pending_streams_to_purge_.front()->removeFromList(pending_streams_to_purge_);
    host_->cluster().trafficStats().upstream_rq_pending_failure_eject_.inc();
    onPoolFailure(host_description, failure_reason, reason, stream->context());
  }
}

bool ConnPoolImplBase::connectingConnectionIsExcess(const ActiveClient& client) const {
  ASSERT(!client.hasHandshakeCompleted());
  ASSERT(connecting_stream_capacity_ >= client.currentUnusedCapacity());
  // If perUpstreamPreconnectRatio is one, this simplifies to checking if there would still be
  // sufficient connecting stream capacity to serve all pending streams if the most recent client
  // were removed from the picture.
  //
  // If preconnect ratio is set, it also factors in the anticipated load based on both queued
  // streams and active streams, and makes sure the connecting capacity would still be sufficient to
  // serve that even with the most recent client removed.
  return (pending_streams_.size() + num_active_streams_) * perUpstreamPreconnectRatio() <=
         (connecting_stream_capacity_ - client.currentUnusedCapacity() + num_active_streams_);
}

void ConnPoolImplBase::onPendingStreamCancel(PendingStream& stream,
                                             Envoy::ConnectionPool::CancelPolicy policy) {
  ENVOY_LOG(debug, "cancelling pending stream");
  if (!pending_streams_to_purge_.empty()) {
    // If pending_streams_to_purge_ is not empty, it means that we are called from
    // with-in a onPoolFailure callback invoked in purgePendingStreams (i.e. purgePendingStreams
    // is down in the call stack). Remove this stream from the list as it is cancelled,
    // and there is no need to call its onPoolFailure callback.
    stream.removeFromList(pending_streams_to_purge_);
  } else {
    state_.decrPendingStreams(1);
    stream.removeFromList(pending_streams_);
  }
  if (policy == Envoy::ConnectionPool::CancelPolicy::CloseExcess) {
    if (!connecting_clients_.empty() &&
        connectingConnectionIsExcess(*connecting_clients_.front())) {
      auto& client = *connecting_clients_.front();
      transitionActiveClientState(client, ActiveClient::State::Draining);
      client.close();
    } else if (!early_data_clients_.empty()) {
      for (ActiveClientPtr& client : early_data_clients_) {
        if (client->numActiveStreams() == 0) {
          // Find an idle early data client and check if it is excess.
          if (connectingConnectionIsExcess(*client)) {
            // Close the client after the for loop avoid messing up with iterator.
            transitionActiveClientState(*client, ActiveClient::State::Draining);
            client->close();
          }
          break;
        }
      }
    }
  }

  host_->cluster().trafficStats().upstream_rq_cancelled_.inc();
  checkForIdleAndCloseIdleConnsIfDraining();
}

void ConnPoolImplBase::decrConnectingAndConnectedStreamCapacity(uint32_t delta,
                                                                ActiveClient& client) {
  state_.decrConnectingAndConnectedStreamCapacity(delta);
  if (!client.hasHandshakeCompleted()) {
    // If still doing handshake, it is contributing to the local connecting stream capacity. Update
    // the capacity as well.
    ASSERT(connecting_stream_capacity_ >= delta);
    connecting_stream_capacity_ -= delta;
  }
}

void ConnPoolImplBase::incrConnectingAndConnectedStreamCapacity(uint32_t delta,
                                                                ActiveClient& client) {
  state_.incrConnectingAndConnectedStreamCapacity(delta);
  if (!client.hasHandshakeCompleted()) {
    connecting_stream_capacity_ += delta;
  }
}

void ConnPoolImplBase::onUpstreamReadyForEarlyData(ActiveClient& client) {
  ASSERT(!client.hasHandshakeCompleted() && client.readyForStream());
  // Check pending streams backward for safe request.
  // Note that this is a O(n) search, but the expected size of pending_streams_ should be small. If
  // this becomes a problem, we could split pending_streams_ into 2 lists.
  auto it = pending_streams_.end();
  if (it == pending_streams_.begin()) {
    return;
  }
  --it;
  while (client.currentUnusedCapacity() > 0) {
    PendingStream& stream = **it;
    bool stop_iteration{false};
    if (it != pending_streams_.begin()) {
      --it;
    } else {
      stop_iteration = true;
    }

    if (stream.can_send_early_data_) {
      ENVOY_CONN_LOG(debug, "creating stream for early data.", client);
      attachStreamToClient(client, stream.context());
      state_.decrPendingStreams(1);
      stream.removeFromList(pending_streams_);
    }
    if (stop_iteration) {
      return;
    }
  }
}

namespace {
// Translate zero to UINT64_MAX so that the zero/unlimited case doesn't
// have to be handled specially.
uint32_t translateZeroToUnlimited(uint32_t limit) {
  return (limit != 0) ? limit : std::numeric_limits<uint32_t>::max();
}
} // namespace

ActiveClient::ActiveClient(ConnPoolImplBase& parent, uint32_t lifetime_stream_limit,
                           uint32_t concurrent_stream_limit)
    : ActiveClient(parent, lifetime_stream_limit, concurrent_stream_limit,
                   concurrent_stream_limit) {}

ActiveClient::ActiveClient(ConnPoolImplBase& parent, uint32_t lifetime_stream_limit,
                           uint32_t effective_concurrent_streams, uint32_t concurrent_stream_limit)
    : parent_(parent), remaining_streams_(translateZeroToUnlimited(lifetime_stream_limit)),
      configured_stream_limit_(translateZeroToUnlimited(effective_concurrent_streams)),
      concurrent_stream_limit_(translateZeroToUnlimited(concurrent_stream_limit)),
      connect_timer_(parent_.dispatcher().createTimer([this]() { onConnectTimeout(); })) {
  conn_connect_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host()->cluster().trafficStats().upstream_cx_connect_ms_,
      parent_.dispatcher().timeSource());
  conn_length_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host()->cluster().trafficStats().upstream_cx_length_ms_,
      parent_.dispatcher().timeSource());
  connect_timer_->enableTimer(parent_.host()->cluster().connectTimeout());
  parent_.host()->stats().cx_total_.inc();
  parent_.host()->stats().cx_active_.inc();
  parent_.host()->cluster().trafficStats().upstream_cx_total_.inc();
  parent_.host()->cluster().trafficStats().upstream_cx_active_.inc();
  parent_.host()->cluster().resourceManager(parent_.priority()).connections().inc();
}

ActiveClient::~ActiveClient() { releaseResourcesBase(); }

void ActiveClient::releaseResourcesBase() {
  if (!resources_released_) {
    resources_released_ = true;

    conn_length_->complete();

    parent_.host()->cluster().trafficStats().upstream_cx_active_.dec();
    parent_.host()->stats().cx_active_.dec();
    parent_.host()->cluster().resourceManager(parent_.priority()).connections().dec();
  }
}

void ActiveClient::onConnectTimeout() {
  ENVOY_CONN_LOG(debug, "connect timeout", *this);
  parent_.host()->cluster().trafficStats().upstream_cx_connect_timeout_.inc();
  timed_out_ = true;
  close();
}

void ActiveClient::onConnectionDurationTimeout() {
  if (!hasHandshakeCompleted()) {
    // The connection duration timer should only have started after we were connected.
    ENVOY_BUG(false, "max connection duration reached while connecting");
    return;
  }

  if (state_ == ActiveClient::State::Closed) {
    // The connection duration timer should have been disabled and reset in onConnectionEvent
    // for closing connections.
    ENVOY_BUG(false, "max connection duration reached while closed");
    return;
  }

  // There's nothing to do if the client is draining.
  if (state_ == ActiveClient::State::Draining) {
    return;
  }

  ENVOY_CONN_LOG(debug, "max connection duration reached, start draining", *this);
  parent_.host()->cluster().trafficStats().upstream_cx_max_duration_reached_.inc();
  parent_.transitionActiveClientState(*this, Envoy::ConnectionPool::ActiveClient::State::Draining);

  // Close out the draining client if we no longer have active streams.
  // We have to do this here because there won't be an onStreamClosed (because there are
  // no active streams) to do it for us later.
  if (numActiveStreams() == 0) {
    close();
  }
}

void ActiveClient::drain() {
  if (currentUnusedCapacity() <= 0) {
    return;
  }
  parent_.decrConnectingAndConnectedStreamCapacity(currentUnusedCapacity(), *this);

  remaining_streams_ = 0;
}

} // namespace ConnectionPool
} // namespace Envoy
