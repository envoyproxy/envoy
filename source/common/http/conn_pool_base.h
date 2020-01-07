#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/http/conn_pool.h"
#include "envoy/stats/timespan.h"

#include "common/common/linked_object.h"
#include "common/http/codec_client.h"

#include "absl/strings/string_view.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Http {

// Base class that handles request queueing logic shared between connection pool implementations.
class ConnPoolImplBase : public ConnectionPool::Instance,
                         protected Logger::Loggable<Logger::Id::pool> {
public:
  // ConnectionPool::Instance
  void addDrainedCallback(DrainedCb cb) override;
  bool hasActiveConnections() const override;
  void drainConnections() override;

protected:
  ConnPoolImplBase(Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                   Event::Dispatcher& dispatcher)
      : host_(host), priority_(priority), dispatcher_(dispatcher) {}
  virtual ~ConnPoolImplBase();

  // ActiveClient provides a base class for connection pool clients that handles connection timings
  // as well as managing the connection timeout.
  class ActiveClient : public LinkedObject<ActiveClient>,
                       public Network::ConnectionCallbacks,
                       public Event::DeferredDeletable {
  public:
    ActiveClient(ConnPoolImplBase& parent);
    virtual ~ActiveClient() { conn_length_->complete(); }

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override {
      base_parent_.onConnectionEvent(*this, event);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    void onConnectTimeout();
    void close() { codec_client_->close(); }

    uint64_t connectionId() const { return codec_client_->id(); }

    virtual bool hasActiveRequests() const PURE;
    virtual bool closingWithIncompleteRequest() const PURE;

    enum class State { CONNECTING, READY, BUSY, DRAINING, CLOSED };
    State state_{State::CONNECTING};
    ConnPoolImplBase& base_parent_; // TODO: combine this and child parent_
    CodecClientPtr codec_client_;
    Upstream::HostDescriptionConstSharedPtr real_host_description_;
    Stats::TimespanPtr conn_connect_ms_;
    Stats::TimespanPtr conn_length_;
    Event::TimerPtr connect_timer_;
  };

  using ActiveClientPtr = std::unique_ptr<ActiveClient>;

  struct PendingRequest : LinkedObject<PendingRequest>, public ConnectionPool::Cancellable {
    PendingRequest(ConnPoolImplBase& parent, StreamDecoder& decoder,
                   ConnectionPool::Callbacks& callbacks);
    ~PendingRequest() override;

    // ConnectionPool::Cancellable
    void cancel() override { parent_.onPendingRequestCancel(*this); }

    ConnPoolImplBase& parent_;
    StreamDecoder& decoder_;
    ConnectionPool::Callbacks& callbacks_;
  };

  using PendingRequestPtr = std::unique_ptr<PendingRequest>;

  // Gets a pointer to the list that currently owns this client.
  std::list<ActiveClientPtr>* owningList(ActiveClient& client);

  // Creates a new PendingRequest and enqueues it into the request queue.
  ConnectionPool::Cancellable* newPendingRequest(StreamDecoder& decoder,
                                                 ConnectionPool::Callbacks& callbacks);
  // Removes the PendingRequest from the list of requests. Called when the PendingRequest is
  // cancelled, e.g. when the stream is reset before a connection has been established.
  void onPendingRequestCancel(PendingRequest& request);

  // Fails all pending requests, calling onPoolFailure on the associated callbacks.
  void purgePendingRequests(const Upstream::HostDescriptionConstSharedPtr& host_description,
                            absl::string_view failure_reason);

  // Closes any idle connections.
  void closeIdleConnections();

  void onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event);

  // Must be implemented by sub class. Attempts to drain inactive clients.
  void checkForDrained();
  virtual void onUpstreamReady() PURE;

  const Upstream::HostConstSharedPtr host_;
  const Upstream::ResourcePriority priority_;
  Event::Dispatcher& dispatcher_;

  std::list<DrainedCb> drained_callbacks_;
  std::list<PendingRequestPtr> pending_requests_;

  // When calling purgePendingRequests, this list will be used to hold the requests we are about
  // to purge. We need this if one cancelled requests cancels a different pending request
  std::list<PendingRequestPtr> pending_requests_to_purge_;

  // Clients that are ready to handle additional requests.
  std::list<ActiveClientPtr> ready_clients_;

  // Clients that are not ready to handle additional requests.
  std::list<ActiveClientPtr> busy_clients_;

  // Clients that are draining but have not completed all outstanding requests yet.
  std::list<ActiveClientPtr> draining_clients_;
};
} // namespace Http
} // namespace Envoy
