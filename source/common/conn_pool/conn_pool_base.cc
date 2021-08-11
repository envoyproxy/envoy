#include "source/common/conn_pool/conn_pool_base.h"

#include "source/common/common/assert.h"
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
    ret += client->effectiveConcurrentStreamLimit();
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
  for (auto* list : {&ready_clients_, &busy_clients_, &connecting_clients_}) {
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
  if (host_->health() != Upstream::Host::Health::Healthy) {
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
  // There are already enough CONNECTING connections for the number of queued streams.
  if (!shouldCreateNewConnection(global_preconnect_ratio)) {
    ENVOY_LOG(trace, "not creating a new connection, shouldCreateNewConnection returned false.");
    return ConnectionResult::ShouldNotConnect;
  }

  const bool can_create_connection =
      host_->cluster().resourceManager(priority_).connections().canCreate();
  if (!can_create_connection) {
    host_->cluster().stats().upstream_cx_overflow_.inc();
  }
  // If we are at the connection circuit-breaker limit due to other upstreams having
  // too many open connections, and this upstream has no connections, always create one, to
  // prevent pending streams being queued to this upstream with no way to be processed.
  if (can_create_connection ||
      (ready_clients_.empty() && busy_clients_.empty() && connecting_clients_.empty())) {
    ENVOY_LOG(debug, "creating a new connection");
    ActiveClientPtr client = instantiateActiveClient();
    if (client.get() == nullptr) {
      ENVOY_LOG(trace, "connection creation failed");
      return ConnectionResult::FailedToCreateConnection;
    }
    ASSERT(client->state() == ActiveClient::State::CONNECTING);
    ASSERT(std::numeric_limits<uint64_t>::max() - connecting_stream_capacity_ >=
           client->effectiveConcurrentStreamLimit());
    ASSERT(client->real_host_description_);
    // Increase the connecting capacity to reflect the streams this connection can serve.
    state_.incrConnectingAndConnectedStreamCapacity(client->effectiveConcurrentStreamLimit());
    connecting_stream_capacity_ += client->effectiveConcurrentStreamLimit();
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
  ASSERT(client.state() == Envoy::ConnectionPool::ActiveClient::State::READY);

  if (!host_->cluster().resourceManager(priority_).requests().canCreate()) {
    ENVOY_LOG(debug, "max streams overflow");
    onPoolFailure(client.real_host_description_, absl::string_view(),
                  ConnectionPool::PoolFailureReason::Overflow, context);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
    return;
  }
  ENVOY_CONN_LOG(debug, "creating stream", client);

  client.remaining_streams_--;
  if (client.remaining_streams_ == 0) {
    ENVOY_CONN_LOG(debug, "maximum streams per connection, DRAINING", client);
    host_->cluster().stats().upstream_cx_max_requests_.inc();
    transitionActiveClientState(client, Envoy::ConnectionPool::ActiveClient::State::DRAINING);
  } else if (client.numActiveStreams() + 1 >= client.concurrent_stream_limit_) {
    // As soon as the new stream is created, the client will be maxed out.
    transitionActiveClientState(client, Envoy::ConnectionPool::ActiveClient::State::BUSY);
  }

  // Decrement the capacity, as there's one less stream available for serving.
  state_.decrConnectingAndConnectedStreamCapacity(1);
  // Track the new active stream.
  state_.incrActiveStreams(1);
  num_active_streams_++;
  host_->stats().rq_total_.inc();
  host_->stats().rq_active_.inc();
  host_->cluster().stats().upstream_rq_total_.inc();
  host_->cluster().stats().upstream_rq_active_.inc();
  host_->cluster().resourceManager(priority_).requests().inc();

  onPoolReady(client, context);
}

void ConnPoolImplBase::onStreamClosed(Envoy::ConnectionPool::ActiveClient& client,
                                      bool delay_attaching_stream) {
  ENVOY_CONN_LOG(debug, "destroying stream: {} remaining", client, client.numActiveStreams());
  ASSERT(num_active_streams_ > 0);
  // Reflect there's one less stream in flight.
  bool had_negative_capacity = client.hadNegativeDeltaOnStreamClosed();
  state_.decrActiveStreams(1);
  num_active_streams_--;
  host_->stats().rq_active_.dec();
  host_->cluster().stats().upstream_rq_active_.dec();
  host_->cluster().resourceManager(priority_).requests().dec();
  // If the effective client capacity was limited by concurrency, increase connecting capacity.
  // If the effective client capacity was limited by max total streams, this will not result in an
  // increment as no capacity is freed up.
  if (client.remaining_streams_ > client.concurrent_stream_limit_ - client.numActiveStreams() - 1 ||
      had_negative_capacity) {
    state_.incrConnectingAndConnectedStreamCapacity(1);
  }
  if (client.state() == ActiveClient::State::DRAINING && client.numActiveStreams() == 0) {
    // Close out the draining client if we no longer have active streams.
    client.close();
  } else if (client.state() == ActiveClient::State::BUSY) {
    transitionActiveClientState(client, ActiveClient::State::READY);
    if (!delay_attaching_stream) {
      onUpstreamReady();
    }
  }
}

ConnectionPool::Cancellable* ConnPoolImplBase::newStream(AttachContext& context) {
  ASSERT(!deferred_deleting_);

  ASSERT(static_cast<ssize_t>(connecting_stream_capacity_) ==
         connectingCapacity(connecting_clients_)); // O(n) debug check.
  if (!ready_clients_.empty()) {
    ActiveClient& client = *ready_clients_.front();
    ENVOY_CONN_LOG(debug, "using existing connection", client);
    attachStreamToClient(client, context);
    // Even if there's a ready client, we may want to preconnect to handle the next incoming stream.
    tryCreateNewConnections();
    return nullptr;
  }

  if (!host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
    ENVOY_LOG(debug, "max pending streams overflow");
    onPoolFailure(nullptr, absl::string_view(), ConnectionPool::PoolFailureReason::Overflow,
                  context);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
    return nullptr;
  }

  ConnectionPool::Cancellable* pending = newPendingStream(context);
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
    onPoolFailure(nullptr, absl::string_view(), ConnectionPool::PoolFailureReason::Overflow,
                  context);
    return nullptr;
  }

  return pending;
}

bool ConnPoolImplBase::maybePreconnect(float global_preconnect_ratio) {
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
}

std::list<ActiveClientPtr>& ConnPoolImplBase::owningList(ActiveClient::State state) {
  switch (state) {
  case ActiveClient::State::CONNECTING:
    return connecting_clients_;
  case ActiveClient::State::READY:
    return ready_clients_;
  case ActiveClient::State::BUSY:
    return busy_clients_;
  case ActiveClient::State::DRAINING:
    return busy_clients_;
  case ActiveClient::State::CLOSED:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void ConnPoolImplBase::transitionActiveClientState(ActiveClient& client,
                                                   ActiveClient::State new_state) {
  auto& old_list = owningList(client.state());
  auto& new_list = owningList(new_state);
  client.setState(new_state);

  // old_list and new_list can be equal when transitioning from BUSY to DRAINING.
  //
  // The documentation for list.splice() (which is what moveBetweenLists() calls) is
  // unclear whether it is allowed for src and dst to be the same, so check here
  // since it is a no-op anyways.
  if (&old_list != &new_list) {
    client.moveBetweenLists(old_list, new_list);
  }
}

void ConnPoolImplBase::addIdleCallbackImpl(Instance::IdleCb cb) { idle_callbacks_.push_back(cb); }

void ConnPoolImplBase::startDrainImpl() {
  is_draining_ = true;
  checkForIdleAndCloseIdleConnsIfDraining();
}

void ConnPoolImplBase::closeIdleConnectionsForDrainingPool() {
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
  }

  for (auto& entry : to_close) {
    entry->close();
  }
}

void ConnPoolImplBase::drainConnectionsImpl() {
  closeIdleConnectionsForDrainingPool();

  // closeIdleConnections() closes all connections in ready_clients_ with no active streams,
  // so all remaining entries in ready_clients_ are serving streams. Move them and all entries
  // in busy_clients_ to draining.
  while (!ready_clients_.empty()) {
    transitionActiveClientState(*ready_clients_.front(), ActiveClient::State::DRAINING);
  }

  // Changing busy_clients_ to DRAINING does not move them between lists,
  // so use a for-loop since the list is not mutated.
  ASSERT(&owningList(ActiveClient::State::DRAINING) == &busy_clients_);
  for (auto& busy_client : busy_clients_) {
    transitionActiveClientState(*busy_client, ActiveClient::State::DRAINING);
  }
}

bool ConnPoolImplBase::isIdleImpl() const {
  return pending_streams_.empty() && ready_clients_.empty() && busy_clients_.empty() &&
         connecting_clients_.empty();
}

void ConnPoolImplBase::checkForIdleAndCloseIdleConnsIfDraining() {
  if (is_draining_) {
    closeIdleConnectionsForDrainingPool();
  }

  if (isIdleImpl()) {
    ENVOY_LOG(debug, "invoking idle callbacks - is_draining_={}", is_draining_);
    for (const Instance::IdleCb& cb : idle_callbacks_) {
      cb();
    }
  }
}

void ConnPoolImplBase::onConnectionEvent(ActiveClient& client, absl::string_view failure_reason,
                                         Network::ConnectionEvent event) {
  if (client.state() == ActiveClient::State::CONNECTING) {
    ASSERT(connecting_stream_capacity_ >= client.effectiveConcurrentStreamLimit());
    connecting_stream_capacity_ -= client.effectiveConcurrentStreamLimit();
  }

  if (client.connect_timer_) {
    client.connect_timer_->disableTimer();
    client.connect_timer_.reset();
  }

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    state_.decrConnectingAndConnectedStreamCapacity(client.currentUnusedCapacity());
    // Make sure that onStreamClosed won't double count.
    client.remaining_streams_ = 0;
    // The client died.
    ENVOY_CONN_LOG(debug, "client disconnected, failure reason: {}", client, failure_reason);

    Envoy::Upstream::reportUpstreamCxDestroy(host_, event);
    const bool incomplete_stream = client.closingWithIncompleteStream();
    if (incomplete_stream) {
      Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);
    }

    if (client.state() == ActiveClient::State::CONNECTING) {
      host_->cluster().stats().upstream_cx_connect_fail_.inc();
      host_->stats().cx_connect_fail_.inc();

      ConnectionPool::PoolFailureReason reason;
      if (client.timed_out_) {
        reason = ConnectionPool::PoolFailureReason::Timeout;
      } else if (event == Network::ConnectionEvent::RemoteClose) {
        reason = ConnectionPool::PoolFailureReason::RemoteConnectionFailure;
      } else {
        reason = ConnectionPool::PoolFailureReason::LocalConnectionFailure;
      }

      // Raw connect failures should never happen under normal circumstances. If we have an upstream
      // that is behaving badly, streams can get stuck here in the pending state. If we see a
      // connect failure, we purge all pending streams so that calling code can determine what to
      // do with the stream.
      // NOTE: We move the existing pending streams to a temporary list. This is done so that
      //       if retry logic submits a new stream to the pool, we don't fail it inline.
      purgePendingStreams(client.real_host_description_, failure_reason, reason);
      // See if we should preconnect based on active connections.
      tryCreateNewConnections();
    }

    // We need to release our resourceManager() resources before checking below for
    // whether we can create a new connection. Normally this would happen when
    // client's destructor runs, but this object needs to be deferredDelete'd(), so
    // this forces part of its cleanup to happen now.
    client.releaseResources();

    dispatcher_.deferredDelete(client.removeFromList(owningList(client.state())));

    checkForIdleAndCloseIdleConnsIfDraining();

    client.setState(ActiveClient::State::CLOSED);

    // If we have pending streams and we just lost a connection we should make a new one.
    if (!pending_streams_.empty()) {
      tryCreateNewConnections();
    }
  } else if (event == Network::ConnectionEvent::Connected) {
    client.conn_connect_ms_->complete();
    client.conn_connect_ms_.reset();
    ASSERT(client.state() == ActiveClient::State::CONNECTING);
    transitionActiveClientState(client, ActiveClient::State::READY);

    // At this point, for the mixed ALPN pool, the client may be deleted. Do not
    // refer to client after this point.
    onConnected(client);
    onUpstreamReady();
    checkForIdleAndCloseIdleConnsIfDraining();
  }
}

PendingStream::PendingStream(ConnPoolImplBase& parent) : parent_(parent) {
  parent_.host()->cluster().stats().upstream_rq_pending_total_.inc();
  parent_.host()->cluster().stats().upstream_rq_pending_active_.inc();
  parent_.host()->cluster().resourceManager(parent_.priority()).pendingRequests().inc();
}

PendingStream::~PendingStream() {
  parent_.host()->cluster().stats().upstream_rq_pending_active_.dec();
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
    host_->cluster().stats().upstream_rq_pending_failure_eject_.inc();
    onPoolFailure(host_description, failure_reason, reason, stream->context());
  }
}

bool ConnPoolImplBase::connectingConnectionIsExcess() const {
  ASSERT(connecting_stream_capacity_ >=
         connecting_clients_.front()->effectiveConcurrentStreamLimit());
  // If perUpstreamPreconnectRatio is one, this simplifies to checking if there would still be
  // sufficient connecting stream capacity to serve all pending streams if the most recent client
  // were removed from the picture.
  //
  // If preconnect ratio is set, it also factors in the anticipated load based on both queued
  // streams and active streams, and makes sure the connecting capacity would still be sufficient to
  // serve that even with the most recent client removed.
  return (pending_streams_.size() + num_active_streams_) * perUpstreamPreconnectRatio() <=
         (connecting_stream_capacity_ -
          connecting_clients_.front()->effectiveConcurrentStreamLimit() + num_active_streams_);
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
  if (policy == Envoy::ConnectionPool::CancelPolicy::CloseExcess && !connecting_clients_.empty() &&
      connectingConnectionIsExcess()) {
    auto& client = *connecting_clients_.front();
    transitionActiveClientState(client, ActiveClient::State::DRAINING);
    client.close();
  }

  host_->cluster().stats().upstream_rq_cancelled_.inc();
  checkForIdleAndCloseIdleConnsIfDraining();
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
    : parent_(parent), remaining_streams_(translateZeroToUnlimited(lifetime_stream_limit)),
      concurrent_stream_limit_(translateZeroToUnlimited(concurrent_stream_limit)),
      connect_timer_(parent_.dispatcher().createTimer([this]() -> void { onConnectTimeout(); })) {
  conn_connect_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host()->cluster().stats().upstream_cx_connect_ms_, parent_.dispatcher().timeSource());
  conn_length_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host()->cluster().stats().upstream_cx_length_ms_, parent_.dispatcher().timeSource());
  connect_timer_->enableTimer(parent_.host()->cluster().connectTimeout());
  parent_.host()->stats().cx_total_.inc();
  parent_.host()->stats().cx_active_.inc();
  parent_.host()->cluster().stats().upstream_cx_total_.inc();
  parent_.host()->cluster().stats().upstream_cx_active_.inc();
  parent_.host()->cluster().resourceManager(parent_.priority()).connections().inc();
}

ActiveClient::~ActiveClient() { releaseResourcesBase(); }

void ActiveClient::onEvent(Network::ConnectionEvent event) {
  parent_.onConnectionEvent(*this, "", event);
}

void ActiveClient::releaseResourcesBase() {
  if (!resources_released_) {
    resources_released_ = true;

    conn_length_->complete();

    parent_.host()->cluster().stats().upstream_cx_active_.dec();
    parent_.host()->stats().cx_active_.dec();
    parent_.host()->cluster().resourceManager(parent_.priority()).connections().dec();
  }
}

void ActiveClient::onConnectTimeout() {
  ENVOY_CONN_LOG(debug, "connect timeout", *this);
  parent_.host()->cluster().stats().upstream_cx_connect_timeout_.inc();
  timed_out_ = true;
  close();
}

void ActiveClient::drain() {
  if (currentUnusedCapacity() <= 0) {
    return;
  }
  if (state() == ActiveClient::State::CONNECTING) {
    // If connecting, update both the cluster capacity and the local connecting
    // capacity.
    parent_.decrConnectingAndConnectedStreamCapacity(currentUnusedCapacity());
  } else {
    parent_.state().decrConnectingAndConnectedStreamCapacity(currentUnusedCapacity());
  }

  remaining_streams_ = 0;
}

} // namespace ConnectionPool
} // namespace Envoy
