#include "common/http/wrapped_connection_pool.h"

namespace Envoy {
namespace Http {

WrappedConnectionPool::WrappedConnectionPool(std::unique_ptr<ConnectionMapper> mapper,
                                             Protocol protocol, Upstream::HostConstSharedPtr host,
                                             Upstream::ResourcePriority priority)
    : ConnPoolImplBase(host, priority), mapper_(std::move(mapper)), protocol_(protocol) {}

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

ConnectionPool::Cancellable*
WrappedConnectionPool::pushPending(Http::StreamDecoder& response_decoder,
                                   ConnectionPool::Callbacks& callbacks,
                                   const Upstream::LoadBalancerContext& /*unused*/) {
  ENVOY_LOG(debug, "queueing request due to no available connection pools");

  /*
  USE ConnPoolBase for this? May just do everything we need.
  if can_queue: use ConnPoolBase
  Answer for now: the mapper takes a callback list. It invokes when a pool is free.
  On pool creation, registers itself with the pool as a drained callback. Future: look into a
  non-destructive way to poll for a pool being idle. Maybe add a different type of callback.
  */
  if (host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
    return newPendingRequest(response_decoder, callbacks);
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

size_t WrappedConnectionPool::numPendingStreams() const { return pending_requests_.size(); }

bool WrappedConnectionPool::drainable() const {
  // TODO(klarose: check whether we have any active pools as well)
  return !drained_callbacks_.empty() && pending_requests_.empty();
}

} // namespace Http
} // namespace Envoy
