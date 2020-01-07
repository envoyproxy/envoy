#include "common/http/conn_pool_base.h"
#include "common/stats/timespan_impl.h"
#include "common/upstream/upstream_impl.h"

#include "common/stats/timespan_impl.h"

namespace Envoy {
namespace Http {
ConnPoolImplBase::ActiveClient::ActiveClient(ConnPoolImplBase& parent)
    : base_parent_(parent), connect_timer_(base_parent_.dispatcher_.createTimer(
                                [this]() -> void { onConnectTimeout(); })) {
  conn_connect_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      base_parent_.host_->cluster().stats().upstream_cx_connect_ms_,
      base_parent_.dispatcher_.timeSource());
  conn_length_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      base_parent_.host_->cluster().stats().upstream_cx_length_ms_,
      base_parent_.dispatcher_.timeSource());
  connect_timer_->enableTimer(base_parent_.host_->cluster().connectTimeout());
}

void ConnPoolImplBase::ActiveClient::onConnectTimeout() {
  ENVOY_CONN_ID_LOG(debug, "connect timeout", connectionId());
  base_parent_.host_->cluster().stats().upstream_cx_connect_timeout_.inc();
  close();
}

ConnPoolImplBase::~ConnPoolImplBase() {
  for (auto* list : {&ready_clients_, &busy_clients_, &draining_clients_}) {
    while (!list->empty()) {
      list->front()->close();
    }
  }

  // Make sure all clients are destroyed before we are destroyed.
  dispatcher_.clearDeferredDeleteList();
}

void ConnPoolImplBase::addDrainedCallback(DrainedCb cb) {
  drained_callbacks_.push_back(cb);
  checkForDrained();
}

bool ConnPoolImplBase::hasActiveConnections() const {
  if (!pending_requests_.empty() || !busy_clients_.empty() || !draining_clients_.empty()) {
    return true;
  }

  for (const auto& ready_client : ready_clients_) {
    if (ready_client->hasActiveRequests()) {
      return true;
    }
  }

  return false;
}

void ConnPoolImplBase::closeIdleConnections() {
  // Create a separate list of elements to close to avoid mutate-while-iterating problems.
  std::list<ActiveClientPtr*> toClose;

  // Possibly-idle connections are always in the ready_clients_ list
  for (auto& client : ready_clients_) {
    if (client->hasActiveRequests()) {
      toClose.push_back(&client);
    }
  }

  while (!toClose.empty()) {
    (*toClose.front())->close();
  }
}

std::list<ConnPoolImplBase::ActiveClientPtr>* ConnPoolImplBase::owningList(ActiveClient& client) {
  switch (client.state_) {
  case ActiveClient::State::CONNECTING:
    return &busy_clients_;
  case ActiveClient::State::READY:
    return &ready_clients_;
  case ActiveClient::State::BUSY:
    return &busy_clients_;
  case ActiveClient::State::DRAINING:
    return &draining_clients_;
  case ActiveClient::State::CLOSED:
    return nullptr;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void ConnPoolImplBase::drainConnections() {
  closeIdleConnections();

  while (!busy_clients_.empty()) {
    auto& client = busy_clients_.front();
    client->state_ = ConnPoolImplBase::ActiveClient::State::DRAINING;
    client->moveBetweenLists(busy_clients_, draining_clients_);
  }
}

void ConnPoolImplBase::checkForDrained() {
  if (drained_callbacks_.empty()) {
    return;
  }

  closeIdleConnections();

  if (pending_requests_.empty() && busy_clients_.empty() && draining_clients_.empty()) {
    ENVOY_LOG(debug, "invoking drained callbacks");
    for (const DrainedCb& cb : drained_callbacks_) {
      cb();
    }
  }
}

void ConnPoolImplBase::onConnectionEvent(ConnPoolImplBase::ActiveClient& client,
                                         Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // The client died.
    ENVOY_CONN_LOG(debug, "client disconnected, failure reason: {}", *client.codec_client_,
                   client.codec_client_->connectionFailureReason());

    Envoy::Upstream::reportUpstreamCxDestroy(host_, event);
    if (client.closingWithIncompleteRequest()) {
      Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);
    }

    ActiveClientPtr removed;
    switch (client.state_) {
    case ActiveClient::State::CONNECTING:
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
      break;

    case ConnPoolImplBase::ActiveClient::State::READY:
      dispatcher_.deferredDelete(client.removeFromList(ready_clients_));
      if (client.closingWithIncompleteRequest()) {
        checkForDrained();
      }
      break;

    case ConnPoolImplBase::ActiveClient::State::BUSY:
      dispatcher_.deferredDelete(client.removeFromList(busy_clients_));
      checkForDrained();
      break;

    case ConnPoolImplBase::ActiveClient::State::DRAINING:
      dispatcher_.deferredDelete(client.removeFromList(draining_clients_));
      checkForDrained();
      break;

    case ConnPoolImplBase::ActiveClient::State::CLOSED:
      PANIC("Connection closed event received while already in CLOSED state.");
      break;
    }
    // bool was_draining = (client.state_ == ConnPoolImplBase::ActiveClient::State::DRAINING);
    client.state_ = ConnPoolImplBase::ActiveClient::State::CLOSED;

    // If we have pending requests and we just lost a connection we should make a new one.
    // TODO(ggreenway): This math only works for http1; figure out how many request slots there
    // are in ready_clients_, and find the CONNECTING clients in busy_clients_ and count them as
    // available.
    /*
    if (!was_draining && pending_requests_.size() > (ready_clients_.size() + busy_clients_.size()))
    { createNewConnection();
    }
    */
  } else if (event == Network::ConnectionEvent::Connected) {
    client.conn_connect_ms_->complete();
    client.conn_connect_ms_.reset();

    ASSERT(client.state_ == ConnPoolImplBase::ActiveClient::State::CONNECTING);
    client.state_ = ConnPoolImplBase::ActiveClient::State::READY;
    client.moveBetweenLists(busy_clients_, ready_clients_);

    onUpstreamReady();
  }

  if (client.connect_timer_) {
    client.connect_timer_->disableTimer();
    client.connect_timer_.reset();
  }
}

ConnPoolImplBase::PendingRequest::PendingRequest(ConnPoolImplBase& parent, StreamDecoder& decoder,
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
ConnPoolImplBase::newPendingRequest(StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks) {
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

} // namespace Http
} // namespace Envoy
