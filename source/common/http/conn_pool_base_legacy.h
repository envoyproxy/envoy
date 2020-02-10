#pragma once

#include "envoy/http/conn_pool.h"
#include "envoy/stats/timespan.h"

#include "common/common/linked_object.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace Legacy {

// Base class that handles request queueing logic shared between connection pool implementations.
class ConnPoolImplBase : protected Logger::Loggable<Logger::Id::pool> {
protected:
  ConnPoolImplBase(Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority)
      : host_(host), priority_(priority) {}
  virtual ~ConnPoolImplBase() = default;

  // ActiveClient provides a base class for connection pool clients that handles connection timings
  // as well as managing the connection timeout.
  class ActiveClient {
  public:
    ActiveClient(Event::Dispatcher& dispatcher, const Upstream::ClusterInfo& cluster);
    virtual ~ActiveClient() { conn_length_->complete(); }

    virtual void onConnectTimeout() PURE;

    void recordConnectionSetup();
    void disarmConnectTimeout();

    enum ConnectionState { Connecting, Connected };
    ConnectionState connectionState();

  private:
    Event::TimerPtr connect_timer_;
    Stats::TimespanPtr conn_connect_ms_;
    Stats::TimespanPtr conn_length_;
  };

  struct PendingRequest : LinkedObject<PendingRequest>, public ConnectionPool::Cancellable {
    PendingRequest(ConnPoolImplBase& parent, ResponseDecoder& decoder,
                   ConnectionPool::Callbacks& callbacks);
    ~PendingRequest() override;

    // ConnectionPool::Cancellable
    void cancel() override { parent_.onPendingRequestCancel(*this); }

    ConnPoolImplBase& parent_;
    ResponseDecoder& decoder_;
    ConnectionPool::Callbacks& callbacks_;
  };

  using PendingRequestPtr = std::unique_ptr<PendingRequest>;

  // Creates a new PendingRequest and enqueues it into the request queue.
  ConnectionPool::Cancellable* newPendingRequest(ResponseDecoder& decoder,
                                                 ConnectionPool::Callbacks& callbacks);
  // Removes the PendingRequest from the list of requests. Called when the PendingRequest is
  // cancelled, e.g. when the stream is reset before a connection has been established.
  void onPendingRequestCancel(PendingRequest& request);

  // Fails all pending requests, calling onPoolFailure on the associated callbacks.
  void purgePendingRequests(const Upstream::HostDescriptionConstSharedPtr& host_description,
                            absl::string_view failure_reason);

  // Must be implemented by sub class. Attempts to drain inactive clients.
  virtual void checkForDrained() PURE;

  const Upstream::HostConstSharedPtr host_;
  const Upstream::ResourcePriority priority_;
  std::list<PendingRequestPtr> pending_requests_;
  // When calling purgePendingRequests, this list will be used to hold the requests we are about
  // to purge. We need this if one cancelled requests cancels a different pending request
  std::list<PendingRequestPtr> pending_requests_to_purge_;
};
} // namespace Legacy
} // namespace Http
} // namespace Envoy
