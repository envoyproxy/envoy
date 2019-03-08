#pragma once

#include "envoy/http/conn_pool.h"

#include "common/common/linked_object.h"

namespace Envoy {
namespace Http {

// Base class that handles request queueing logic shared between connection pool implementations.
class ConnPoolImplBase : protected Logger::Loggable<Logger::Id::pool> {
protected:
  ConnPoolImplBase(Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority)
      : host_(host), priority_(priority) {}
  virtual ~ConnPoolImplBase() = default;

  struct PendingRequest : LinkedObject<PendingRequest>, public ConnectionPool::Cancellable {
    PendingRequest(ConnPoolImplBase& parent, StreamDecoder& decoder,
                   ConnectionPool::Callbacks& callbacks);
    ~PendingRequest();

    // ConnectionPool::Cancellable
    void cancel() override { parent_.onPendingRequestCancel(*this); }

    ConnPoolImplBase& parent_;
    StreamDecoder& decoder_;
    ConnectionPool::Callbacks& callbacks_;
  };

  typedef std::unique_ptr<PendingRequest> PendingRequestPtr;

  // Creates a new PendingRequest and enqueues it into the request queue.
  ConnectionPool::Cancellable* newPendingRequest(StreamDecoder& decoder,
                                                 ConnectionPool::Callbacks& callbacks);
  // Removes the PendingRequest from the list of requests. Called when the PendingRequest is
  // cancelled, e.g. when the stream is reset before a connection has been established.
  void onPendingRequestCancel(PendingRequest& request);

  // Fails all pending requests, calling onPoolFailure on the associated callbacks.
  void purgePendingRequests(const Upstream::HostDescriptionConstSharedPtr& host_description);

  // Must be implemented by sub class. Attempts to drain inactive clients.
  virtual void checkForDrained() PURE;

  const Upstream::HostConstSharedPtr host_;
  const Upstream::ResourcePriority priority_;
  std::list<PendingRequestPtr> pending_requests_;
};
} // namespace Http
} // namespace Envoy
