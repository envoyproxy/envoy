#include "common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {
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
