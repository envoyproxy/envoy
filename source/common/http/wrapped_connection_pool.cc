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

ConnectionPool::Cancellable*
WrappedConnectionPool::newStream(Http::StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks,
                                 const Upstream::LoadBalancerContext& context) {
  auto wrapper = std::make_unique<PendingWrapper>(decoder, callbacks, context, *this);
  Instance* pool = mapper_->assignPool(context);
  if (!pool) {
    return pushPending(std::move(wrapper), decoder, callbacks, context);
  }

  // grab a reference so when we move it into the list, we still have it!
  PendingWrapper& wrapper_ref = *wrapper;
  wrapper->moveIntoList(std::move(wrapper), wrapped_waiting_);
  ConnectionPool::Cancellable* cancellable = pool->newStream(decoder, wrapper_ref, context);

  if (cancellable) {
    wrapper_ref.setWaitingCancelCallback(*cancellable);
    return &wrapper_ref;
  }

  return nullptr;
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
size_t WrappedConnectionPool::numPendingStreams() const { return wrapped_pending_.size(); }

WrappedConnectionPool::PendingWrapper::PendingWrapper(Http::StreamDecoder& decoder,
                                                      ConnectionPool::Callbacks& callbacks,
                                                      const Upstream::LoadBalancerContext& context,
                                                      WrappedConnectionPool& parent)
    : decoder_(decoder), wrapped_callbacks_(callbacks), context_(context),
      wrapped_pending_(nullptr), waiting_cancel_(nullptr), parent_(parent) {}

WrappedConnectionPool::PendingWrapper::~PendingWrapper() = default;

void WrappedConnectionPool::PendingWrapper::cancel() {
  // we should only be called in a state where wrapped_cancel is not null.
  ASSERT(wrapped_pending_ != nullptr || waiting_cancel_ != nullptr);
  if (wrapped_pending_) {
    wrapped_pending_->cancel();
    parent_.onWrappedRequestPendingCancel(*this);
    return;
  }

  if (waiting_cancel_) {
    waiting_cancel_->cancel();
  }

  parent_.onWrappedRequestWaitingFinished(*this);
}
void WrappedConnectionPool::PendingWrapper::onPoolFailure(
    ConnectionPool::PoolFailureReason reason, Upstream::HostDescriptionConstSharedPtr host) {
  wrapped_callbacks_.onPoolFailure(reason, std::move(host));
  parent_.onWrappedRequestWaitingFinished(*this);
}
void WrappedConnectionPool::PendingWrapper::onPoolReady(
    Http::StreamEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host) {
  wrapped_callbacks_.onPoolReady(encoder, std::move(host));
  parent_.onWrappedRequestWaitingFinished(*this);
}

bool WrappedConnectionPool::PendingWrapper::allocatePending(
    ConnectionMapper& mapper, std::list<ConnPoolImplBase::PendingRequestPtr>& pending_list) {
  if (!wrapped_pending_) {
    return false;
  }

  Instance* pool = mapper.assignPool(context_);

  if (!pool) {
    return false;
  }

  wrapped_pending_->removeFromList(pending_list);
  wrapped_pending_ = nullptr;

  ConnectionPool::Cancellable* cancellable = pool->newStream(decoder_, *this, context_);

  waiting_cancel_ = cancellable;
  return true;
}

ConnectionPool::Cancellable* WrappedConnectionPool::pushPending(
    std::unique_ptr<PendingWrapper> wrapper, Http::StreamDecoder& response_decoder,
    ConnectionPool::Callbacks& callbacks, const Upstream::LoadBalancerContext& lb_context) {
  ENVOY_LOG(debug, "queueing request due to no available connection pools");

  if (host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
    ConnPoolImplBase::PendingRequest* pending =
        newPendingRequest(response_decoder, callbacks, &lb_context);
    wrapper->setPendingRequest(*pending);
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

  // note: at this point the wrapper is blown away due to the unique_ptr going out of scope.
  return nullptr;
}

bool WrappedConnectionPool::drainable() const {
  // TODO(klarose: check whether we have any active pools as well)
  return !drained_callbacks_.empty() && pending_requests_.empty() && wrapped_waiting_.empty();
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
    // UGHUGHUGH. This won't work. :( We need to be in wrapped_waiting_ when the "ready" comes from
    // the pool. It could come immediately. So, we either need to handle this not being in the list,
    // which is hard, or we need to do this first. Probably best to do it first.
    to_move->moveBetweenLists(wrapped_pending_, wrapped_waiting_);
  }
}

void WrappedConnectionPool::onWrappedRequestPendingCancel(PendingWrapper& wrapper) {
  wrapper.removeFromList(wrapped_pending_);
}

void WrappedConnectionPool::onWrappedRequestWaitingFinished(PendingWrapper& wrapper) {
  wrapper.removeFromList(wrapped_waiting_);
}
} // namespace Http
} // namespace Envoy
