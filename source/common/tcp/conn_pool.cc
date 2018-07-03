#include "common/tcp/conn_pool.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Tcp {

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                           Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options)
    : dispatcher_(dispatcher), host_(host), priority_(priority), socket_options_(options),
      upstream_ready_timer_(dispatcher_.createTimer([this]() { onUpstreamReady(); })) {}

ConnPoolImpl::~ConnPoolImpl() {
  while (!ready_conns_.empty()) {
    ready_conns_.front()->conn_->close(Network::ConnectionCloseType::NoFlush);
  }

  while (!busy_conns_.empty()) {
    busy_conns_.front()->conn_->close(Network::ConnectionCloseType::NoFlush);
  }

  // Make sure all connections are destroyed before we are destroyed.
  dispatcher_.clearDeferredDeleteList();
}

void ConnPoolImpl::drainConnections() {
  while (!ready_conns_.empty()) {
    ready_conns_.front()->conn_->close(Network::ConnectionCloseType::NoFlush);
  }

  // We drain busy connections by manually setting remaining requests to 1. Thus, when the next
  // response completes the connection will be destroyed.
  for (const auto& conn : busy_conns_) {
    conn->remaining_requests_ = 1;
  }
}

void ConnPoolImpl::addDrainedCallback(DrainedCb cb) {
  drained_callbacks_.push_back(cb);
  checkForDrained();
}

void ConnPoolImpl::assignConnection(ActiveConn& conn, ConnectionPool::Callbacks& callbacks) {
  ASSERT(conn.wrapper_ == nullptr);
  conn.wrapper_ = std::make_unique<ConnectionWrapper>(conn);
  callbacks.onPoolReady(*conn.wrapper_, conn.real_host_description_);
}

void ConnPoolImpl::checkForDrained() {
  if (!drained_callbacks_.empty() && pending_requests_.empty() && busy_conns_.empty()) {
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
  conn->moveIntoList(std::move(conn), busy_conns_);
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
    if ((ready_conns_.size() == 0 && busy_conns_.size() == 0) || can_create_connection) {
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
    ActiveConnPtr removed;
    bool check_for_drained = true;
    if (conn.wrapper_ != nullptr) {
      if (!conn.wrapper_->released_) {
        if (event == Network::ConnectionEvent::LocalClose) {
          host_->cluster().stats().upstream_cx_destroy_local_with_active_rq_.inc();
        }
        if (event == Network::ConnectionEvent::RemoteClose) {
          host_->cluster().stats().upstream_cx_destroy_remote_with_active_rq_.inc();
        }
        host_->cluster().stats().upstream_cx_destroy_with_active_rq_.inc();
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
      removed = conn.removeFromList(busy_conns_);

      // Raw connect failures should never happen under normal circumstances. If we have an upstream
      // that is behaving badly, requests can get stuck here in the pending state. If we see a
      // connect failure, we purge all pending requests so that calling code can determine what to
      // do with the request.
      // NOTE: We move the existing pending requests to a temporary list. This is done so that
      //       if retry logic submits a new request to the pool, we don't fail it inline.
      std::list<PendingRequestPtr> pending_requests_to_purge(std::move(pending_requests_));
      while (!pending_requests_to_purge.empty()) {
        PendingRequestPtr request =
            pending_requests_to_purge.front()->removeFromList(pending_requests_to_purge);
        host_->cluster().stats().upstream_rq_pending_failure_eject_.inc();
        request->callbacks_.onPoolFailure(ConnectionPool::PoolFailureReason::ConnectionFailure,
                                          conn.real_host_description_);
      }
    }

    dispatcher_.deferredDelete(std::move(removed));

    // If we have pending requests and we just lost a connection we should make a new one.
    if (pending_requests_.size() > (ready_conns_.size() + busy_conns_.size())) {
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
  // whether the connection is in the ready list (connected) or the busy list (failed to connect).
  if (event == Network::ConnectionEvent::Connected) {
    conn_connect_ms_->complete();
    processIdleConnection(conn, false);
  }
}

void ConnPoolImpl::onPendingRequestCancel(PendingRequest& request) {
  ENVOY_LOG(debug, "canceling pending request");
  request.removeFromList(pending_requests_);
  host_->cluster().stats().upstream_rq_cancelled_.inc();
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
    processIdleConnection(conn, true);
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
    assignConnection(conn, pending_requests_.back()->callbacks_);
    pending_requests_.pop_back();
    conn.moveBetweenLists(ready_conns_, busy_conns_);
  }
}

void ConnPoolImpl::processIdleConnection(ActiveConn& conn, bool delay) {
  conn.wrapper_.reset();
  if (pending_requests_.empty() || delay) {
    // There is nothing to service or delayed processing is requested, so just move the connection
    // into the ready list.
    ENVOY_CONN_LOG(debug, "moving to ready", *conn.conn_);
    conn.moveBetweenLists(busy_conns_, ready_conns_);
  } else {
    // There is work to do immediately so bind a request to the caller and move it to the busy list.
    // Pending requests are pushed onto the front, so pull from the back.
    ENVOY_CONN_LOG(debug, "assigning connection", *conn.conn_);
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

ConnPoolImpl::ConnectionWrapper::~ConnectionWrapper() {
  parent_.parent_.host_->cluster().stats().upstream_rq_active_.dec();
  parent_.parent_.host_->stats().rq_active_.dec();
}

Network::ClientConnection& ConnPoolImpl::ConnectionWrapper::connection() { return *parent_.conn_; }

void ConnPoolImpl::ConnectionWrapper::addUpstreamCallbacks(ConnectionPool::UpstreamCallbacks& cb) {
  ASSERT(!released_);
  callbacks_ = &cb;
}

void ConnPoolImpl::ConnectionWrapper::release() {
  ASSERT(!released_);
  released_ = true;
  callbacks_ = nullptr;
  parent_.parent_.onConnReleased(parent_);
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
      remaining_requests_(parent_.host_->cluster().maxRequestsPerConnection()) {

  parent_.conn_connect_ms_.reset(
      new Stats::Timespan(parent_.host_->cluster().stats().upstream_cx_connect_ms_));

  Upstream::Host::CreateConnectionData data =
      parent_.host_->createConnection(parent_.dispatcher_, parent_.socket_options_);
  real_host_description_ = data.host_description_;

  conn_ = std::move(data.connection_);

  conn_->detectEarlyCloseWhenReadDisabled(false);
  conn_->addConnectionCallbacks(*this);
  conn_->addReadFilter(Network::ReadFilterSharedPtr{new ConnReadFilter(*this)});

  ENVOY_CONN_LOG(debug, "connecting", *conn_);
  conn_->connect();

  parent_.host_->cluster().stats().upstream_cx_total_.inc();
  parent_.host_->cluster().stats().upstream_cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_http1_total_.inc();
  parent_.host_->stats().cx_total_.inc();
  parent_.host_->stats().cx_active_.inc();
  conn_length_.reset(new Stats::Timespan(parent_.host_->cluster().stats().upstream_cx_length_ms_));
  connect_timer_->enableTimer(parent_.host_->cluster().connectTimeout());
  parent_.host_->cluster().resourceManager(parent_.priority_).connections().inc();

  conn_->setConnectionStats({parent_.host_->cluster().stats().upstream_cx_rx_bytes_total_,
                             parent_.host_->cluster().stats().upstream_cx_rx_bytes_buffered_,
                             parent_.host_->cluster().stats().upstream_cx_tx_bytes_total_,
                             parent_.host_->cluster().stats().upstream_cx_tx_bytes_buffered_,
                             &parent_.host_->cluster().stats().bind_errors_});

  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  conn_->noDelay(true);
}

ConnPoolImpl::ActiveConn::~ActiveConn() {
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

} // namespace Tcp
} // namespace Envoy
