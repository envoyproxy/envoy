#include "common/http/conn_pool_base.h"

#include "common/stats/timespan_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Http {
ConnPoolImplBase::ConnPoolImplBase(
    Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
    : host_(host), priority_(priority), dispatcher_(dispatcher), socket_options_(options),
      transport_socket_options_(transport_socket_options) {}

ConnPoolImplBase::~ConnPoolImplBase() {
  ASSERT(ready_clients_.empty());
  ASSERT(busy_clients_.empty());
}

void ConnPoolImplBase::destructAllConnections() {
  for (auto* list : {&ready_clients_, &busy_clients_}) {
    while (!list->empty()) {
      list->front()->close();
    }
  }

  // Make sure all clients are destroyed before we are destroyed.
  dispatcher_.clearDeferredDeleteList();
}

void ConnPoolImplBase::tryCreateNewConnection() {
  if (pending_requests_.size() <= connecting_request_capacity_) {
    // There are already enough CONNECTING connections for the number
    // of queued requests.
    return;
  }

  const bool can_create_connection =
      host_->cluster().resourceManager(priority_).connections().canCreate();
  if (!can_create_connection) {
    host_->cluster().stats().upstream_cx_overflow_.inc();
  }
  // If we are at the connection circuit-breaker limit due to other upstreams having
  // too many open connections, and this upstream has no connections, always create one, to
  // prevent pending requests being queued to this upstream with no way to be processed.
  if (can_create_connection || (ready_clients_.empty() && busy_clients_.empty())) {
    ENVOY_LOG(debug, "creating a new connection");
    ActiveClientPtr client = instantiateActiveClient();
    ASSERT(client->state_ == ActiveClient::State::CONNECTING);
    ASSERT(std::numeric_limits<uint64_t>::max() - connecting_request_capacity_ >=
           client->effectiveConcurrentRequestLimit());
    connecting_request_capacity_ += client->effectiveConcurrentRequestLimit();
    client->moveIntoList(std::move(client), owningList(client->state_));
  }
}

void ConnPoolImplBase::attachRequestToClient(ActiveClient& client,
                                             ResponseDecoder& response_decoder,
                                             ConnectionPool::Callbacks& callbacks) {
  ASSERT(client.state_ == ActiveClient::State::READY);

  if (!host_->cluster().resourceManager(priority_).requests().canCreate()) {
    ENVOY_LOG(debug, "max requests overflow");
    callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, absl::string_view(),
                            nullptr);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
  } else {
    ENVOY_CONN_LOG(debug, "creating stream", *client.codec_client_);
    RequestEncoder& new_encoder = client.newStreamEncoder(response_decoder);

    client.remaining_requests_--;
    if (client.remaining_requests_ == 0) {
      ENVOY_CONN_LOG(debug, "maximum requests per connection, DRAINING", *client.codec_client_);
      host_->cluster().stats().upstream_cx_max_requests_.inc();
      transitionActiveClientState(client, ActiveClient::State::DRAINING);
    } else if (client.codec_client_->numActiveRequests() >= client.concurrent_request_limit_) {
      transitionActiveClientState(client, ActiveClient::State::BUSY);
    }

    num_active_requests_++;
    host_->stats().rq_total_.inc();
    host_->stats().rq_active_.inc();
    host_->cluster().stats().upstream_rq_total_.inc();
    host_->cluster().stats().upstream_rq_active_.inc();
    host_->cluster().resourceManager(priority_).requests().inc();
    callbacks.onPoolReady(new_encoder, client.real_host_description_,
                          client.codec_client_->streamInfo());
  }
}

void ConnPoolImplBase::onRequestClosed(ActiveClient& client, bool delay_attaching_request) {
  ENVOY_CONN_LOG(debug, "destroying stream: {} remaining", *client.codec_client_,
                 client.codec_client_->numActiveRequests());
  ASSERT(num_active_requests_ > 0);
  num_active_requests_--;
  host_->stats().rq_active_.dec();
  host_->cluster().stats().upstream_rq_active_.dec();
  host_->cluster().resourceManager(priority_).requests().dec();
  if (client.state_ == ActiveClient::State::DRAINING &&
      client.codec_client_->numActiveRequests() == 0) {
    // Close out the draining client if we no longer have active requests.
    client.codec_client_->close();
  } else if (client.state_ == ActiveClient::State::BUSY) {
    // A request was just ended, so we should be below the limit now.
    ASSERT(client.codec_client_->numActiveRequests() < client.concurrent_request_limit_);

    transitionActiveClientState(client, ActiveClient::State::READY);
    if (!delay_attaching_request) {
      onUpstreamReady();
    }
  }
}

ConnectionPool::Cancellable* ConnPoolImplBase::newStream(ResponseDecoder& response_decoder,
                                                         ConnectionPool::Callbacks& callbacks) {
  if (!ready_clients_.empty()) {
    ActiveClient& client = *ready_clients_.front();
    ENVOY_CONN_LOG(debug, "using existing connection", *client.codec_client_);
    attachRequestToClient(client, response_decoder, callbacks);
    return nullptr;
  }

  if (host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
    ConnectionPool::Cancellable* pending = newPendingRequest(response_decoder, callbacks);

    // This must come after newPendingRequest() because this function uses the
    // length of pending_requests_ to determine if a new connection is needed.
    tryCreateNewConnection();

    return pending;
  } else {
    ENVOY_LOG(debug, "max pending requests overflow");
    callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, absl::string_view(),
                            nullptr);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
    return nullptr;
  }
}

void ConnPoolImplBase::onUpstreamReady() {
  while (!pending_requests_.empty() && !ready_clients_.empty()) {
    ActiveClientPtr& client = ready_clients_.front();
    ENVOY_CONN_LOG(debug, "attaching to next request", *client->codec_client_);
    // Pending requests are pushed onto the front, so pull from the back.
    attachRequestToClient(*client, pending_requests_.back()->decoder_,
                          pending_requests_.back()->callbacks_);
    pending_requests_.pop_back();
  }
}

bool ConnPoolImplBase::hasActiveConnections() const {
  return (!pending_requests_.empty() || (num_active_requests_ > 0));
}

std::list<ConnPoolImplBase::ActiveClientPtr>&
ConnPoolImplBase::owningList(ActiveClient::State state) {
  switch (state) {
  case ActiveClient::State::CONNECTING:
    return busy_clients_;
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
  auto& old_list = owningList(client.state_);
  auto& new_list = owningList(new_state);
  client.state_ = new_state;

  // old_list and new_list can be equal when transitioning from BUSY to DRAINING.
  //
  // The documentation for list.splice() (which is what moveBetweenLists() calls) is
  // unclear whether it is allowed for src and dst to be the same, so check here
  // since it is a no-op anyways.
  if (&old_list != &new_list) {
    client.moveBetweenLists(old_list, new_list);
  }
}

void ConnPoolImplBase::addDrainedCallback(DrainedCb cb) {
  drained_callbacks_.push_back(cb);
  checkForDrained();
}

void ConnPoolImplBase::closeIdleConnections() {
  // Create a separate list of elements to close to avoid mutate-while-iterating problems.
  std::list<ActiveClient*> to_close;

  for (auto& client : ready_clients_) {
    if (!client->hasActiveRequests()) {
      to_close.push_back(client.get());
    }
  }

  if (pending_requests_.empty()) {
    for (auto& client : busy_clients_) {
      if (client->state_ == ActiveClient::State::CONNECTING) {
        to_close.push_back(client.get());
      }
    }
  }

  for (auto& entry : to_close) {
    entry->close();
  }
}

void ConnPoolImplBase::drainConnections() {
  closeIdleConnections();

  // closeIdleConnections() closes all connections in ready_clients_ with no active requests,
  // so all remaining entries in ready_clients_ are serving requests. Move them and all entries
  // in busy_clients_ to draining.
  while (!ready_clients_.empty()) {
    transitionActiveClientState(*ready_clients_.front(), ActiveClient::State::DRAINING);
  }

  // Changing busy_clients_ to DRAINING does not move them between lists,
  // so use a for-loop since the list is not mutated.
  ASSERT(&owningList(ActiveClient::State::DRAINING) == &busy_clients_);
  for (auto& busy_client : busy_clients_) {
    // Moving a CONNECTING client to DRAINING would violate state assumptions, namely that DRAINING
    // connections have active requests (otherwise they would be closed) and that clients receiving
    // a Connected event are in state CONNECTING.
    if (busy_client->state_ != ActiveClient::State::CONNECTING) {
      transitionActiveClientState(*busy_client, ActiveClient::State::DRAINING);
    }
  }
}

void ConnPoolImplBase::checkForDrained() {
  if (drained_callbacks_.empty()) {
    return;
  }

  closeIdleConnections();

  if (pending_requests_.empty() && ready_clients_.empty() && busy_clients_.empty()) {
    ENVOY_LOG(debug, "invoking drained callbacks");
    for (const DrainedCb& cb : drained_callbacks_) {
      cb();
    }
  }
}

void ConnPoolImplBase::onConnectionEvent(ConnPoolImplBase::ActiveClient& client,
                                         Network::ConnectionEvent event) {
  if (client.state_ == ActiveClient::State::CONNECTING) {
    ASSERT(connecting_request_capacity_ >= client.effectiveConcurrentRequestLimit());
    connecting_request_capacity_ -= client.effectiveConcurrentRequestLimit();
  }

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // The client died.
    ENVOY_CONN_LOG(debug, "client disconnected, failure reason: {}", *client.codec_client_,
                   client.codec_client_->connectionFailureReason());

    Envoy::Upstream::reportUpstreamCxDestroy(host_, event);
    const bool incomplete_request = client.closingWithIncompleteRequest();
    if (incomplete_request) {
      Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);
    }

    if (client.state_ == ActiveClient::State::CONNECTING) {
      host_->cluster().stats().upstream_cx_connect_fail_.inc();
      host_->stats().cx_connect_fail_.inc();

      // Raw connect failures should never happen under normal circumstances. If we have an upstream
      // that is behaving badly, requests can get stuck here in the pending state. If we see a
      // connect failure, we purge all pending requests so that calling code can determine what to
      // do with the request.
      // NOTE: We move the existing pending requests to a temporary list. This is done so that
      //       if retry logic submits a new request to the pool, we don't fail it inline.
      purgePendingRequests(client.real_host_description_,
                           client.codec_client_->connectionFailureReason());
    }

    // We need to release our resourceManager() resources before checking below for
    // whether we can create a new connection. Normally this would happen when
    // client's destructor runs, but this object needs to be deferredDelete'd(), so
    // this forces part of its cleanup to happen now.
    client.releaseResources();

    dispatcher_.deferredDelete(client.removeFromList(owningList(client.state_)));
    if (incomplete_request) {
      checkForDrained();
    }

    client.state_ = ActiveClient::State::CLOSED;

    // If we have pending requests and we just lost a connection we should make a new one.
    if (!pending_requests_.empty()) {
      tryCreateNewConnection();
    }
  } else if (event == Network::ConnectionEvent::Connected) {
    client.conn_connect_ms_->complete();
    client.conn_connect_ms_.reset();

    ASSERT(client.state_ == ActiveClient::State::CONNECTING);
    transitionActiveClientState(client, ActiveClient::State::READY);

    onUpstreamReady();
    checkForDrained();
  }

  if (client.connect_timer_) {
    client.connect_timer_->disableTimer();
    client.connect_timer_.reset();
  }
}

ConnPoolImplBase::PendingRequest::PendingRequest(ConnPoolImplBase& parent, ResponseDecoder& decoder,
                                                 ConnectionPool::Callbacks& callbacks)
    : parent_(parent), decoder_(decoder), callbacks_(callbacks) {
  parent_.host_->cluster().stats().upstream_rq_pending_total_.inc();
  parent_.host_->cluster().stats().upstream_rq_pending_active_.inc();
  parent_.host_->cluster().resourceManager(parent_.priority_).pendingRequests().inc();
}

ConnPoolImplBase::PendingRequest::~PendingRequest() {
  parent_.host_->cluster().stats().upstream_rq_pending_active_.dec();
  parent_.host_->cluster().resourceManager(parent_.priority_).pendingRequests().dec();
}

ConnectionPool::Cancellable*
ConnPoolImplBase::newPendingRequest(ResponseDecoder& decoder,
                                    ConnectionPool::Callbacks& callbacks) {
  ENVOY_LOG(debug, "queueing request due to no available connections");
  PendingRequestPtr pending_request(new PendingRequest(*this, decoder, callbacks));
  pending_request->moveIntoList(std::move(pending_request), pending_requests_);
  return pending_requests_.front().get();
}

void ConnPoolImplBase::purgePendingRequests(
    const Upstream::HostDescriptionConstSharedPtr& host_description,
    absl::string_view failure_reason) {
  // NOTE: We move the existing pending requests to a temporary list. This is done so that
  //       if retry logic submits a new request to the pool, we don't fail it inline.
  pending_requests_to_purge_ = std::move(pending_requests_);
  while (!pending_requests_to_purge_.empty()) {
    PendingRequestPtr request =
        pending_requests_to_purge_.front()->removeFromList(pending_requests_to_purge_);
    host_->cluster().stats().upstream_rq_pending_failure_eject_.inc();
    request->callbacks_.onPoolFailure(ConnectionPool::PoolFailureReason::ConnectionFailure,
                                      failure_reason, host_description);
  }
}

void ConnPoolImplBase::onPendingRequestCancel(PendingRequest& request) {
  ENVOY_LOG(debug, "cancelling pending request");
  if (!pending_requests_to_purge_.empty()) {
    // If pending_requests_to_purge_ is not empty, it means that we are called from
    // with-in a onPoolFailure callback invoked in purgePendingRequests (i.e. purgePendingRequests
    // is down in the call stack). Remove this request from the list as it is cancelled,
    // and there is no need to call its onPoolFailure callback.
    request.removeFromList(pending_requests_to_purge_);
  } else {
    request.removeFromList(pending_requests_);
  }

  host_->cluster().stats().upstream_rq_cancelled_.inc();
  checkForDrained();
}

namespace {
// Translate zero to UINT64_MAX so that the zero/unlimited case doesn't
// have to be handled specially.
uint64_t translateZeroToUnlimited(uint64_t limit) {
  return (limit != 0) ? limit : std::numeric_limits<uint64_t>::max();
}
} // namespace

ConnPoolImplBase::ActiveClient::ActiveClient(ConnPoolImplBase& parent,
                                             uint64_t lifetime_request_limit,
                                             uint64_t concurrent_request_limit)
    : parent_(parent), remaining_requests_(translateZeroToUnlimited(lifetime_request_limit)),
      concurrent_request_limit_(translateZeroToUnlimited(concurrent_request_limit)),
      connect_timer_(parent_.dispatcher_.createTimer([this]() -> void { onConnectTimeout(); })) {
  Upstream::Host::CreateConnectionData data = parent_.host_->createConnection(
      parent_.dispatcher_, parent_.socket_options_, parent_.transport_socket_options_);
  real_host_description_ = data.host_description_;
  codec_client_ = parent_.createCodecClient(data);
  codec_client_->addConnectionCallbacks(*this);

  conn_connect_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host_->cluster().stats().upstream_cx_connect_ms_, parent_.dispatcher_.timeSource());
  conn_length_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host_->cluster().stats().upstream_cx_length_ms_, parent_.dispatcher_.timeSource());
  connect_timer_->enableTimer(parent_.host_->cluster().connectTimeout());

  parent_.host_->stats().cx_total_.inc();
  parent_.host_->stats().cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_total_.inc();
  parent_.host_->cluster().stats().upstream_cx_active_.inc();
  parent_.host_->cluster().resourceManager(parent_.priority_).connections().inc();

  codec_client_->setConnectionStats(
      {parent_.host_->cluster().stats().upstream_cx_rx_bytes_total_,
       parent_.host_->cluster().stats().upstream_cx_rx_bytes_buffered_,
       parent_.host_->cluster().stats().upstream_cx_tx_bytes_total_,
       parent_.host_->cluster().stats().upstream_cx_tx_bytes_buffered_,
       &parent_.host_->cluster().stats().bind_errors_, nullptr});
}

ConnPoolImplBase::ActiveClient::~ActiveClient() { releaseResources(); }

void ConnPoolImplBase::ActiveClient::releaseResources() {
  if (!resources_released_) {
    resources_released_ = true;

    conn_length_->complete();

    parent_.host_->cluster().stats().upstream_cx_active_.dec();
    parent_.host_->stats().cx_active_.dec();
    parent_.host_->cluster().resourceManager(parent_.priority_).connections().dec();
  }
}

void ConnPoolImplBase::ActiveClient::onConnectTimeout() {
  ENVOY_CONN_LOG(debug, "connect timeout", *codec_client_);
  parent_.host_->cluster().stats().upstream_cx_connect_timeout_.inc();
  close();
}

} // namespace Http
} // namespace Envoy
