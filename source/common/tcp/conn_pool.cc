#include "common/tcp/conn_pool.h"

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/upstream/upstream.h"

#include "common/stats/timespan_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Tcp {

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                           Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options,
                           Network::TransportSocketOptionsSharedPtr transport_socket_options)
    : dispatcher_(dispatcher), host_(host), priority_(priority), socket_options_(options),
      transport_socket_options_(transport_socket_options),
      upstream_ready_timer_(dispatcher_.createTimer([this]() { onUpstreamReady(); })) {}

ConnPoolImpl::~ConnPoolImpl() {
  while (!ready_conns_.empty()) {
    ready_conns_.front()->conn_->close(Network::ConnectionCloseType::NoFlush);
  }

  while (!busy_conns_.empty()) {
    busy_conns_.front()->conn_->close(Network::ConnectionCloseType::NoFlush);
  }

  while (!pending_conns_.empty()) {
    pending_conns_.front()->conn_->close(Network::ConnectionCloseType::NoFlush);
  }

  // Make sure all connections are destroyed before we are destroyed.
  dispatcher_.clearDeferredDeleteList();
}

void ConnPoolImpl::drainConnections() {
  while (!ready_conns_.empty()) {
    ready_conns_.front()->conn_->close(Network::ConnectionCloseType::NoFlush);
  }

  // We drain busy and pending connections by manually setting remaining requests to 1. Thus, when
  // the next response completes the connection will be destroyed.
  for (const auto& conn : busy_conns_) {
    conn->remaining_requests_ = 1;
  }

  for (const auto& conn : pending_conns_) {
    conn->remaining_requests_ = 1;
  }
}

void ConnPoolImpl::addDrainedCallback(DrainedCb cb) {
  drained_callbacks_.push_back(cb);
  checkForDrained();
}

void ConnPoolImpl::assignConnection(ActiveConn& conn, ConnectionPool::Callbacks& callbacks) {
  ASSERT(conn.wrapper_ == nullptr);
  conn.wrapper_ = std::make_shared<ConnectionWrapper>(conn);

  callbacks.onPoolReady(std::make_unique<ConnectionDataImpl>(conn.wrapper_),
                        conn.real_host_description_);
}

void ConnPoolImpl::checkForDrained() {
  if (!drained_callbacks_.empty() && pending_requests_.empty() && busy_conns_.empty() &&
      pending_conns_.empty()) {
    while (!ready_conns_.empty()) {
      ready_conns_.front()->conn_->close(Network::ConnectionCloseType::NoFlush);
    }

    for (const DrainedCb& cb : drained_callbacks_) {
      cb();
    }
  }
}

void ConnPoolImpl::createNewConnection() {
  ENVOY_LOG(debug, "creating a new connection");
  ActiveConnPtr conn(new ActiveConn(*this));
  conn->moveIntoList(std::move(conn), pending_conns_);
}

ConnectionPool::Cancellable* ConnPoolImpl::newConnection(ConnectionPool::Callbacks& callbacks) {
  if (!ready_conns_.empty()) {
    ready_conns_.front()->moveBetweenLists(ready_conns_, busy_conns_);
    ENVOY_CONN_LOG(debug, "using existing connection", *busy_conns_.front()->conn_);
    assignConnection(*busy_conns_.front(), callbacks);
    return nullptr;
  }

  if (host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
    bool can_create_connection =
        host_->cluster().resourceManager(priority_).connections().canCreate();
    if (!can_create_connection) {
      host_->cluster().stats().upstream_cx_overflow_.inc();
    }

    // If we have no connections at all, make one no matter what so we don't starve.
    if ((ready_conns_.empty() && busy_conns_.empty() && pending_conns_.empty()) ||
        can_create_connection) {
      createNewConnection();
    }

    ENVOY_LOG(debug, "queueing request due to no available connections");
    PendingRequestPtr pending_request(new PendingRequest(*this, callbacks));
    pending_request->moveIntoList(std::move(pending_request), pending_requests_);
    return pending_requests_.front().get();
  } else {
    ENVOY_LOG(debug, "max pending requests overflow");
    callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, nullptr);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
    return nullptr;
  }
}

void ConnPoolImpl::onConnectionEvent(ActiveConn& conn, Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_CONN_LOG(debug, "client disconnected", *conn.conn_);

    Envoy::Upstream::reportUpstreamCxDestroy(host_, event);

    ActiveConnPtr removed;
    bool check_for_drained = true;
    if (conn.wrapper_ != nullptr) {
      if (!conn.wrapper_->released_) {
        Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);

        conn.wrapper_->release(true);
      }

      removed = conn.removeFromList(busy_conns_);
    } else if (!conn.connect_timer_) {
      // The connect timer is destroyed on connect. The lack of a connect timer means that this
      // connection is idle and in the ready pool.
      removed = conn.removeFromList(ready_conns_);
      check_for_drained = false;
    } else {
      // The only time this happens is if we actually saw a connect failure.
      host_->cluster().stats().upstream_cx_connect_fail_.inc();
      host_->stats().cx_connect_fail_.inc();
      removed = conn.removeFromList(pending_conns_);

      // Raw connect failures should never happen under normal circumstances. If we have an upstream
      // that is behaving badly, requests can get stuck here in the pending state. If we see a
      // connect failure, we purge all pending requests so that calling code can determine what to
      // do with the request.
      // NOTE: We move the existing pending requests to a temporary list. This is done so that
      //       if retry logic submits a new request to the pool, we don't fail it inline.
      // TODO(lizan): If pool failure due to transport socket, propagate the reason to access log.
      ConnectionPool::PoolFailureReason reason;
      if (conn.timed_out_) {
        reason = ConnectionPool::PoolFailureReason::Timeout;
      } else if (event == Network::ConnectionEvent::RemoteClose) {
        reason = ConnectionPool::PoolFailureReason::RemoteConnectionFailure;
      } else {
        reason = ConnectionPool::PoolFailureReason::LocalConnectionFailure;
      }

      std::list<PendingRequestPtr> pending_requests_to_purge(std::move(pending_requests_));
      while (!pending_requests_to_purge.empty()) {
        PendingRequestPtr request =
            pending_requests_to_purge.front()->removeFromList(pending_requests_to_purge);
        host_->cluster().stats().upstream_rq_pending_failure_eject_.inc();
        request->callbacks_.onPoolFailure(reason, conn.real_host_description_);
      }
    }

    dispatcher_.deferredDelete(std::move(removed));

    // If we have pending requests and we just lost a connection we should make a new one.
    if (pending_requests_.size() >
        (ready_conns_.size() + busy_conns_.size() + pending_conns_.size())) {
      createNewConnection();
    }

    if (check_for_drained) {
      checkForDrained();
    }
  }

  if (conn.connect_timer_) {
    conn.connect_timer_->disableTimer();
    conn.connect_timer_.reset();
  }

  // Note that the order in this function is important. Concretely, we must destroy the connect
  // timer before we process an idle connection, because if this results in an immediate
  // drain/destruction event, we key off of the existence of the connect timer above to determine
  // whether the connection is in the ready list (connected) or the pending list (failed to
  // connect).
  if (event == Network::ConnectionEvent::Connected) {
    conn.conn_->streamInfo().setDownstreamSslConnection(conn.conn_->ssl());
    conn_connect_ms_->complete();
    processIdleConnection(conn, true, false);
  }
}

void ConnPoolImpl::onPendingRequestCancel(PendingRequest& request,
                                          ConnectionPool::CancelPolicy cancel_policy) {
  ENVOY_LOG(debug, "canceling pending request");
  request.removeFromList(pending_requests_);
  host_->cluster().stats().upstream_rq_cancelled_.inc();

  // If the cancel requests closure of excess connections and there are more pending connections
  // than requests, close the most recently created pending connection.
  if (cancel_policy == ConnectionPool::CancelPolicy::CloseExcess &&
      pending_requests_.size() < pending_conns_.size()) {
    ENVOY_LOG(debug, "canceling pending connection");
    pending_conns_.back()->conn_->close(Network::ConnectionCloseType::NoFlush);
  }

  checkForDrained();
}

void ConnPoolImpl::onConnReleased(ActiveConn& conn) {
  ENVOY_CONN_LOG(debug, "connection released", *conn.conn_);

  if (conn.remaining_requests_ > 0 && --conn.remaining_requests_ == 0) {
    ENVOY_CONN_LOG(debug, "maximum requests per connection", *conn.conn_);
    host_->cluster().stats().upstream_cx_max_requests_.inc();

    conn.conn_->close(Network::ConnectionCloseType::NoFlush);
  } else {
    // Upstream connection might be closed right after response is complete. Setting delay=true
    // here to assign pending requests in next dispatcher loop to handle that case.
    // https://github.com/envoyproxy/envoy/issues/2715
    processIdleConnection(conn, false, true);
  }
}

void ConnPoolImpl::onConnDestroyed(ActiveConn& conn) {
  ENVOY_CONN_LOG(debug, "connection destroyed", *conn.conn_);
}

void ConnPoolImpl::onUpstreamReady() {
  upstream_ready_enabled_ = false;
  while (!pending_requests_.empty() && !ready_conns_.empty()) {
    ActiveConn& conn = *ready_conns_.front();
    ENVOY_CONN_LOG(debug, "assigning connection", *conn.conn_);
    // There is work to do so bind a connection to the caller and move it to the busy list. Pending
    // requests are pushed onto the front, so pull from the back.
    conn.moveBetweenLists(ready_conns_, busy_conns_);
    assignConnection(conn, pending_requests_.back()->callbacks_);
    pending_requests_.pop_back();
  }
}

void ConnPoolImpl::processIdleConnection(ActiveConn& conn, bool new_connection, bool delay) {
  if (conn.wrapper_) {
    conn.wrapper_->invalidate();
    conn.wrapper_.reset();
  }

  // TODO(zuercher): As a future improvement, we may wish to close extra connections when there are
  // no pending requests rather than moving them to ready_conns_. For conn pool callers that re-use
  // connections it is possible that a busy connection may be re-assigned to a pending request
  // while a new connection is pending. The current behavior is to move the pending connection to
  // the ready list to await a future request. For some protocols, e.g. mysql which has the server
  // transmit handshake data on connect, it may be desirable to close the connection if no pending
  // request is available. The CloseExcess flag for cancel is related: if we close pending
  // connections without requests here it becomes superfluous (instead of closing connections at
  // cancel time we'd wait until they completed and close them here). Finally, we want to avoid
  // requiring operators to correct configure clusters to get the necessary pending connection
  // behavior (e.g. we want to find a way to enable the new behavior without having to configure
  // it on a cluster).

  if (pending_requests_.empty() || delay) {
    // There is nothing to service or delayed processing is requested, so just move the connection
    // into the ready list.
    ENVOY_CONN_LOG(debug, "moving to ready", *conn.conn_);
    if (new_connection) {
      conn.moveBetweenLists(pending_conns_, ready_conns_);
    } else {
      conn.moveBetweenLists(busy_conns_, ready_conns_);
    }
  } else {
    // There is work to do immediately so bind a request to the caller and move it to the busy list.
    // Pending requests are pushed onto the front, so pull from the back.
    ENVOY_CONN_LOG(debug, "assigning connection", *conn.conn_);
    if (new_connection) {
      conn.moveBetweenLists(pending_conns_, busy_conns_);
    }
    assignConnection(conn, pending_requests_.back()->callbacks_);
    pending_requests_.pop_back();
  }

  if (delay && !pending_requests_.empty() && !upstream_ready_enabled_) {
    upstream_ready_enabled_ = true;
    upstream_ready_timer_->enableTimer(std::chrono::milliseconds(0));
  }

  checkForDrained();
}

ConnPoolImpl::ConnectionWrapper::ConnectionWrapper(ActiveConn& parent) : parent_(parent) {
  parent_.parent_.host_->cluster().stats().upstream_rq_total_.inc();
  parent_.parent_.host_->cluster().stats().upstream_rq_active_.inc();
  parent_.parent_.host_->stats().rq_total_.inc();
  parent_.parent_.host_->stats().rq_active_.inc();
}

Network::ClientConnection& ConnPoolImpl::ConnectionWrapper::connection() {
  ASSERT(conn_valid_);
  return *parent_.conn_;
}

void ConnPoolImpl::ConnectionWrapper::addUpstreamCallbacks(ConnectionPool::UpstreamCallbacks& cb) {
  ASSERT(!released_);
  callbacks_ = &cb;
}

void ConnPoolImpl::ConnectionWrapper::release(bool closed) {
  // Allow multiple calls: connection close and destruction of ConnectionDataImplPtr will both
  // result in this call.
  if (!released_) {
    released_ = true;
    callbacks_ = nullptr;
    if (!closed) {
      parent_.parent_.onConnReleased(parent_);
    }

    parent_.parent_.host_->cluster().stats().upstream_rq_active_.dec();
    parent_.parent_.host_->stats().rq_active_.dec();
  }
}

ConnPoolImpl::PendingRequest::PendingRequest(ConnPoolImpl& parent,
                                             ConnectionPool::Callbacks& callbacks)
    : parent_(parent), callbacks_(callbacks) {
  parent_.host_->cluster().stats().upstream_rq_pending_total_.inc();
  parent_.host_->cluster().stats().upstream_rq_pending_active_.inc();
  parent_.host_->cluster().resourceManager(parent_.priority_).pendingRequests().inc();
}

ConnPoolImpl::PendingRequest::~PendingRequest() {
  parent_.host_->cluster().stats().upstream_rq_pending_active_.dec();
  parent_.host_->cluster().resourceManager(parent_.priority_).pendingRequests().dec();
}

ConnPoolImpl::ActiveConn::ActiveConn(ConnPoolImpl& parent)
    : parent_(parent),
      connect_timer_(parent_.dispatcher_.createTimer([this]() -> void { onConnectTimeout(); })),
      remaining_requests_(parent_.host_->cluster().maxRequestsPerConnection()), timed_out_(false) {

  parent_.conn_connect_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host_->cluster().stats().upstream_cx_connect_ms_, parent_.dispatcher_.timeSource());

  Upstream::Host::CreateConnectionData data = parent_.host_->createConnection(
      parent_.dispatcher_, parent_.socket_options_, parent_.transport_socket_options_);
  real_host_description_ = data.host_description_;

  conn_ = std::move(data.connection_);

  conn_->detectEarlyCloseWhenReadDisabled(false);
  conn_->addConnectionCallbacks(*this);
  conn_->addReadFilter(Network::ReadFilterSharedPtr{new ConnReadFilter(*this)});

  ENVOY_CONN_LOG(debug, "connecting", *conn_);
  conn_->connect();

  parent_.host_->cluster().stats().upstream_cx_total_.inc();
  parent_.host_->cluster().stats().upstream_cx_active_.inc();
  parent_.host_->stats().cx_total_.inc();
  parent_.host_->stats().cx_active_.inc();
  conn_length_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host_->cluster().stats().upstream_cx_length_ms_, parent_.dispatcher_.timeSource());
  connect_timer_->enableTimer(parent_.host_->cluster().connectTimeout());
  parent_.host_->cluster().resourceManager(parent_.priority_).connections().inc();

  conn_->setConnectionStats({parent_.host_->cluster().stats().upstream_cx_rx_bytes_total_,
                             parent_.host_->cluster().stats().upstream_cx_rx_bytes_buffered_,
                             parent_.host_->cluster().stats().upstream_cx_tx_bytes_total_,
                             parent_.host_->cluster().stats().upstream_cx_tx_bytes_buffered_,
                             &parent_.host_->cluster().stats().bind_errors_, nullptr});

  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  conn_->noDelay(true);
}

ConnPoolImpl::ActiveConn::~ActiveConn() {
  if (wrapper_) {
    wrapper_->invalidate();
  }

  parent_.host_->cluster().stats().upstream_cx_active_.dec();
  parent_.host_->stats().cx_active_.dec();
  conn_length_->complete();
  parent_.host_->cluster().resourceManager(parent_.priority_).connections().dec();

  parent_.onConnDestroyed(*this);
}

void ConnPoolImpl::ActiveConn::onConnectTimeout() {
  // We just close the connection at this point. This will result in both a timeout and a connect
  // failure and will fold into all the normal connect failure logic.
  ENVOY_CONN_LOG(debug, "connect timeout", *conn_);
  timed_out_ = true;
  parent_.host_->cluster().stats().upstream_cx_connect_timeout_.inc();
  conn_->close(Network::ConnectionCloseType::NoFlush);
}

void ConnPoolImpl::ActiveConn::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  if (wrapper_ != nullptr && wrapper_->callbacks_ != nullptr) {
    // Delegate to the connection owner.
    wrapper_->callbacks_->onUpstreamData(data, end_stream);
  } else {
    // Unexpected data from upstream, close down the connection.
    ENVOY_CONN_LOG(debug, "unexpected data from upstream, closing connection", *conn_);
    conn_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void ConnPoolImpl::ActiveConn::onEvent(Network::ConnectionEvent event) {
  ConnectionPool::UpstreamCallbacks* cb = nullptr;
  if (wrapper_ != nullptr && wrapper_->callbacks_ != nullptr) {
    cb = wrapper_->callbacks_;
  }

  // In the event of a close event, we want to update the pool's state before triggering callbacks,
  // preventing the case where we attempt to return a closed connection to the ready pool.
  parent_.onConnectionEvent(*this, event);

  if (cb) {
    cb->onEvent(event);
  }
}

void ConnPoolImpl::ActiveConn::onAboveWriteBufferHighWatermark() {
  if (wrapper_ != nullptr && wrapper_->callbacks_ != nullptr) {
    wrapper_->callbacks_->onAboveWriteBufferHighWatermark();
  }
}

void ConnPoolImpl::ActiveConn::onBelowWriteBufferLowWatermark() {
  if (wrapper_ != nullptr && wrapper_->callbacks_ != nullptr) {
    wrapper_->callbacks_->onBelowWriteBufferLowWatermark();
  }
}

} // namespace Tcp
} // namespace Envoy
