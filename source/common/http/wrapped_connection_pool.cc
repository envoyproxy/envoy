#include "common/http/wrapped_connection_pool.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Http {

WrappedConnectionPool::WrappedConnectionPool(std::unique_ptr<ConnectionMapper> mapper,
                                             Protocol protocol, Upstream::HostConstSharedPtr host,
                                             Upstream::ResourcePriority priority)
    : ConnPoolImplBase(host, priority), mapper_(std::move(mapper)), protocol_(protocol) {
  mapper_->addIdleCallback([this] { allocatePendingRequests(); });
}

Http::Protocol WrappedConnectionPool::protocol() const { return protocol_; }

void WrappedConnectionPool::addDrainedCallback(DrainedCb cb) { drained_callbacks_.push_back(cb); }

void WrappedConnectionPool::drainConnections() {}

// TODO(klarose: Add an onPool Idle function so we know to deal with pending requests)

ConnectionPool::Cancellable*
WrappedConnectionPool::newStream(Http::StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks,
                                 const Upstream::LoadBalancerContext& context) {
  // TODO(klarose: Next up. Figure out how to register with the connection pool for idle
  // connections.
  Instance* pool = mapper_->assignPool(context);
  if (!pool) {
    return pushPending(decoder, callbacks, context);
  }

  return pool->newStream(decoder, callbacks, context);
}

void WrappedConnectionPool::checkForDrained() {
  if (!drainable()) {
    return;
  }

  // TODO(klarose: we shouldn't need to recursively notify sub-pools. By registering a callback,
  // we'll be notified when they're all cleaned up, so we can move them directly into the empty
  // pool. Note: This may not be desireable from a perf perspective)
  for (const DrainedCb& cb : drained_callbacks_) {
    cb();
  }
}

size_t WrappedConnectionPool::numWaitingStreams() const { return wrapped_waiting_.size(); }

WrappedConnectionPool::PendingWrapper::PendingWrapper(ConnPoolImplBase::PendingRequest& to_wrap,
                                                      WrappedConnectionPool& parent)
    : wrapped_pending_(&to_wrap), wrapped_cancel_(&to_wrap), parent_(parent) {}

WrappedConnectionPool::PendingWrapper::~PendingWrapper() = default;

void WrappedConnectionPool::PendingWrapper::cancel() {
  // we should only be called in a state where wrapped_cancel is not null.
  ASSERT(wrapped_cancel_ != nullptr);
  wrapped_cancel_->cancel();
  if (wrapped_pending_) {
    parent_.onWrappedRequestPendingCancel(*this);
  } else {
    parent_.onWrappedRequestWaitingFinished(*this);
  }
}

bool WrappedConnectionPool::PendingWrapper::allocatePending(
    ConnectionMapper& mapper, std::list<ConnPoolImplBase::PendingRequestPtr>& pending_list) {
  if (!wrapped_pending_) {
    return false;
  }

  const Upstream::LoadBalancerContext* context = wrapped_pending_->lb_context_;
  // context should never be null. It should only get here from us calling newPendingRequest
  // with the pointer from the reference in newStream. However, assert for good measure in case
  // somebody changes ConnPoolImplBase's behaviour.
  ASSERT(context != nullptr);
  Instance* pool = mapper.assignPool(*context);

  if (!pool) {
    return false;
  }

  ConnectionPool::Cancellable* cancellable =
      pool->newStream(wrapped_pending_->decoder_, wrapped_pending_->callbacks_, *context);
  wrapped_cancel_ = cancellable;
  wrapped_pending_->removeFromList(pending_list);
  wrapped_pending_ = nullptr;
  return true;
}

ConnectionPool::Cancellable*
WrappedConnectionPool::pushPending(Http::StreamDecoder& response_decoder,
                                   ConnectionPool::Callbacks& callbacks,
                                   const Upstream::LoadBalancerContext& lb_context) {
  ENVOY_LOG(debug, "queueing request due to no available connection pools");

  /*
  USE ConnPoolBase for this? May just do everything we need.
  if can_queue: use ConnPoolBase
  Answer for now: the mapper takes a callback list. It invokes when a pool is free.
  On pool creation, registers itself with the pool as a drained callback. Future: look into a
  non-destructive way to poll for a pool being idle. Maybe add a different type of callback.
  */
  if (host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
    ConnPoolImplBase::PendingRequest* pending =
        newPendingRequest(response_decoder, callbacks, &lb_context);
    PendingWrapperPtr wrapper = std::make_unique<PendingWrapper>(*pending, *this);
    wrapper->moveIntoList(std::move(wrapper), wrapped_pending_);
    return wrapped_pending_.front().get();
  }

  ENVOY_LOG(debug, "max pending requests overflow");
  // unfortunately, we need to increment these here. Normally we wouldn't, because a partioned pool
  // would do it for us. But, since there aren't any, we have to.
  host_->cluster().stats().upstream_rq_total_.inc();
  host_->stats().rq_total_.inc();
  callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, nullptr);
  host_->cluster().stats().upstream_rq_pending_overflow_.inc();
  return nullptr;
}

bool WrappedConnectionPool::drainable() const {
  // TODO(klarose: check whether we have any active pools as well)
  return !drained_callbacks_.empty() && pending_requests_.empty();
}

void WrappedConnectionPool::allocatePendingRequests() {
  // for simplicitly, we simply iterate through each pending request and see if it can be assigned.
  // we do this, since we don't know which requests will be assigned to which pools. It's possible
  // that every request could be assigned to a single free pool, so go through them all at the
  // expense of potentially processing more than necessary.
  auto pending_itr = wrapped_pending_.begin();
  while (pending_itr != wrapped_pending_.end()) {
    if (!(*pending_itr)->allocatePending(*mapper_, pending_requests_)) {
      ++pending_itr;
      continue;
    }
    PendingWrapper* to_move = (pending_itr++)->get();
    to_move->moveBetweenLists(wrapped_pending_, wrapped_waiting_);
  }

  // TODO(klarose: How do we handle the pool returning a cancellable?)
  /*
  1. Wrap the cancelable in our own.
  2. That cancellable will have a secondary callout which is invoked on cancel.
  3. By default it is the original cancellable (from the base)
  4. When we assign to a new stream, if return another cancelable, we update.
  ^ Done. Next up:
  5. Problem: how do we know when the connection is established to free up the pending thing?
      --Need to insert ourselves into the callbacks. But, that is in itself nasty. But, I guess,
      necessary.
  6. To do 5, it looks like we need to do two things:
      a) We move the pending request into a new list for sub-pending requests.
      b) When we create a new request, we add ourself as the callback
      c) The callback will be called with a failure or a ready.
      d) In both cases we clean up the internal resources in the sub-pending request
      e) Then we call out with the original call.
  */
}

void WrappedConnectionPool::onWrappedRequestPendingCancel(PendingWrapper& wrapper) {
  wrapper.removeFromList(wrapped_pending_);
}

void WrappedConnectionPool::onWrappedRequestWaitingFinished(PendingWrapper& wrapper) {
  wrapper.removeFromList(wrapped_waiting_);
}
} // namespace Http
} // namespace Envoy
