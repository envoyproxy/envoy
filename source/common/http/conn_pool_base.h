#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/stats/timespan.h"

#include "common/common/linked_object.h"
#include "common/http/codec_client.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

// Base class that handles request queueing logic shared between connection pool implementations.
class ConnPoolImplBase : public ConnectionPool::Instance,
                         protected Logger::Loggable<Logger::Id::pool> {
public:
  // ConnectionPool::Instance
  ConnectionPool::Cancellable* newStream(ResponseDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) override;
  void addDrainedCallback(DrainedCb cb) override;
  bool hasActiveConnections() const override;
  void drainConnections() override;
  Upstream::HostDescriptionConstSharedPtr host() const override { return host_; };

protected:
  ConnPoolImplBase(Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                   Event::Dispatcher& dispatcher,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsSharedPtr& transport_socket_options);
  virtual ~ConnPoolImplBase();

  // Closes and destroys all connections. This must be called in the destructor of
  // derived classes because the derived ActiveClient will downcast parent_ to a more
  // specific type of ConnPoolImplBase, but if the more specific part is already destructed
  // (due to bottom-up destructor ordering in c++) that access will be invalid.
  void destructAllConnections();

  // ActiveClient provides a base class for connection pool clients that handles connection timings
  // as well as managing the connection timeout.
  class ActiveClient : public LinkedObject<ActiveClient>,
                       public Network::ConnectionCallbacks,
                       public Event::DeferredDeletable {
  public:
    ActiveClient(ConnPoolImplBase& parent, uint64_t lifetime_request_limit,
                 uint64_t concurrent_request_limit);
    virtual ~ActiveClient();

    void releaseResources();

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override {
      parent_.onConnectionEvent(*this, event);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    void onConnectTimeout();
    void close() { codec_client_->close(); }

    // Returns the concurrent request limit, accounting for if the total request limit
    // is less than the concurrent request limit.
    uint64_t effectiveConcurrentRequestLimit() const {
      return std::min(remaining_requests_, concurrent_request_limit_);
    }

    virtual bool hasActiveRequests() const PURE;
    virtual bool closingWithIncompleteRequest() const PURE;
    virtual RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) PURE;

    enum class State {
      CONNECTING, // Connection is not yet established.
      READY,      // Additional requests may be immediately dispatched to this connection.
      BUSY,       // Connection is at its concurrent request limit.
      DRAINING,   // No more requests can be dispatched to this connection, and it will be closed
                  // when all requests complete.
      CLOSED      // Connection is closed and object is queued for destruction.
    };

    ConnPoolImplBase& parent_;
    uint64_t remaining_requests_;
    const uint64_t concurrent_request_limit_;
    State state_{State::CONNECTING};
    CodecClientPtr codec_client_;
    Upstream::HostDescriptionConstSharedPtr real_host_description_;
    Stats::TimespanPtr conn_connect_ms_;
    Stats::TimespanPtr conn_length_;
    Event::TimerPtr connect_timer_;
    bool resources_released_{false};
  };

  using ActiveClientPtr = std::unique_ptr<ActiveClient>;

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

  // Create a new CodecClient.
  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;

  // Returns a new instance of ActiveClient.
  virtual ActiveClientPtr instantiateActiveClient() PURE;

  // Gets a pointer to the list that currently owns this client.
  std::list<ActiveClientPtr>& owningList(ActiveClient::State state);

  // Creates a new PendingRequest and enqueues it into the request queue.
  ConnectionPool::Cancellable* newPendingRequest(ResponseDecoder& decoder,
                                                 ConnectionPool::Callbacks& callbacks);
  // Removes the PendingRequest from the list of requests. Called when the PendingRequest is
  // cancelled, e.g. when the stream is reset before a connection has been established.
  void onPendingRequestCancel(PendingRequest& request);

  // Fails all pending requests, calling onPoolFailure on the associated callbacks.
  void purgePendingRequests(const Upstream::HostDescriptionConstSharedPtr& host_description,
                            absl::string_view failure_reason);

  // Closes any idle connections.
  void closeIdleConnections();

  // Called by derived classes any time a request is completed or destroyed for any reason.
  void onRequestClosed(ActiveClient& client, bool delay_attaching_request);

  // Changes the state_ of an ActiveClient and moves to the appropriate list.
  void transitionActiveClientState(ActiveClient& client, ActiveClient::State new_state);

  void onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event);
  void checkForDrained();
  void onUpstreamReady();
  void attachRequestToClient(ActiveClient& client, ResponseDecoder& response_decoder,
                             ConnectionPool::Callbacks& callbacks);

  // Creates a new connection if allowed by resourceManager, or if created to avoid
  // starving this pool.
  void tryCreateNewConnection();

public:
  const Upstream::HostConstSharedPtr host_;
  const Upstream::ResourcePriority priority_;

protected:
  Event::Dispatcher& dispatcher_;
  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;
  const Network::TransportSocketOptionsSharedPtr transport_socket_options_;

  std::list<DrainedCb> drained_callbacks_;
  std::list<PendingRequestPtr> pending_requests_;

  // When calling purgePendingRequests, this list will be used to hold the requests we are about
  // to purge. We need this if one cancelled requests cancels a different pending request
  std::list<PendingRequestPtr> pending_requests_to_purge_;

  // Clients that are ready to handle additional requests.
  // All entries are in state READY.
  std::list<ActiveClientPtr> ready_clients_;

  // Clients that are not ready to handle additional requests.
  // Entries are in possible states CONNECTING, BUSY, or DRAINING.
  std::list<ActiveClientPtr> busy_clients_;

  // The number of requests currently attached to clients.
  uint64_t num_active_requests_{0};

  // The number of requests that can be immediately dispatched
  // if all CONNECTING connections become connected.
  uint64_t connecting_request_capacity_{0};
};
} // namespace Http
} // namespace Envoy
