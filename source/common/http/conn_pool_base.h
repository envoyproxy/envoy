#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/stats/timespan.h"

#include "common/common/linked_object.h"
#include "common/http/codec_client.h"

#include "absl/strings/string_view.h"

namespace Envoy {
// TODO(alyssawilk) move all the code in this namespace to // source/common/conn_pool/ in
// a follow up, in the hopes git will preserve history.
namespace ConnectionPool {

class ConnPoolImplBase;

// ActiveClient provides a base class for connection pool clients that handles connection timings
// as well as managing the connection timeout.
class ActiveClient : public LinkedObject<ActiveClient>,
                     public Network::ConnectionCallbacks,
                     public Event::DeferredDeletable,
                     protected Logger::Loggable<Logger::Id::pool> {
public:
  ActiveClient(ConnPoolImplBase& parent, uint64_t lifetime_request_limit,
               uint64_t concurrent_request_limit);
  ~ActiveClient() override;

  void releaseResources();

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Called if the connection does not complete within the cluster's connectTimeout()
  void onConnectTimeout();

  // Returns the concurrent request limit, accounting for if the total request limit
  // is less than the concurrent request limit.
  uint64_t effectiveConcurrentRequestLimit() const {
    return std::min(remaining_requests_, concurrent_request_limit_);
  }

  // Closes the underlying connection.
  virtual void close() PURE;
  // Returns the ID of the underlying connection.
  virtual uint64_t id() const PURE;
  // Returns true if this closed with an incomplete request, for stats tracking/ purposes.
  virtual bool closingWithIncompleteRequest() const PURE;
  // Returns the number of active requests on this connection.
  virtual size_t numActiveRequests() const PURE;

  enum class State {
    CONNECTING, // Connection is not yet established.
    READY,      // Additional requests may be immediately dispatched to this connection.
    BUSY,       // Connection is at its concurrent request limit.
    DRAINING,   // No more requests can be dispatched to this connection, and it will be closed
    // when all requests complete.
    CLOSED // Connection is closed and object is queued for destruction.
  };

  ConnPoolImplBase& parent_;
  uint64_t remaining_requests_;
  const uint64_t concurrent_request_limit_;
  State state_{State::CONNECTING};
  Upstream::HostDescriptionConstSharedPtr real_host_description_;
  Stats::TimespanPtr conn_connect_ms_;
  Stats::TimespanPtr conn_length_;
  Event::TimerPtr connect_timer_;
  bool resources_released_{false};
  bool timed_out_{false};
};

// PendingRequest is the base class for a connection which has been created but not yet established.
class PendingRequest : public LinkedObject<PendingRequest>, public ConnectionPool::Cancellable {
public:
  PendingRequest(ConnPoolImplBase& parent);
  ~PendingRequest() override;

  // ConnectionPool::Cancellable
  void cancel(Envoy::ConnectionPool::CancelPolicy policy) override;

  // The context here returns a pointer to whatever context is provided with newStream(),
  // which will be passed back to the parent in onPoolReady or onPoolFailure.
  virtual void* context() PURE;

  ConnPoolImplBase& parent_;
};

using PendingRequestPtr = std::unique_ptr<PendingRequest>;

using ActiveClientPtr = std::unique_ptr<ActiveClient>;

// Base class that handles request queueing logic shared between connection pool implementations.
class ConnPoolImplBase : protected Logger::Loggable<Logger::Id::pool> {
public:
  ConnPoolImplBase(Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                   Event::Dispatcher& dispatcher,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsSharedPtr& transport_socket_options);
  virtual ~ConnPoolImplBase();

  void addDrainedCallbackImpl(Instance::DrainedCb cb);
  void drainConnectionsImpl();

  // Closes and destroys all connections. This must be called in the destructor of
  // derived classes because the derived ActiveClient will downcast parent_ to a more
  // specific type of ConnPoolImplBase, but if the more specific part is already destructed
  // (due to bottom-up destructor ordering in c++) that access will be invalid.
  void destructAllConnections();

  // Returns a new instance of ActiveClient.
  virtual ActiveClientPtr instantiateActiveClient() PURE;

  // Gets a pointer to the list that currently owns this client.
  std::list<ActiveClientPtr>& owningList(ActiveClient::State state);

  // Removes the PendingRequest from the list of requests. Called when the PendingRequest is
  // cancelled, e.g. when the stream is reset before a connection has been established.
  void onPendingRequestCancel(PendingRequest& request, Envoy::ConnectionPool::CancelPolicy policy);

  // Fails all pending requests, calling onPoolFailure on the associated callbacks.
  void purgePendingRequests(const Upstream::HostDescriptionConstSharedPtr& host_description,
                            absl::string_view failure_reason,
                            ConnectionPool::PoolFailureReason pool_failure_reason);

  // Closes any idle connections.
  void closeIdleConnections();

  // Changes the state_ of an ActiveClient and moves to the appropriate list.
  void transitionActiveClientState(ActiveClient& client, ActiveClient::State new_state);

  void onConnectionEvent(ActiveClient& client, absl::string_view failure_reason,
                         Network::ConnectionEvent event);
  void checkForDrained();
  void onUpstreamReady();
  ConnectionPool::Cancellable* newStream(void* context);

  virtual ConnectionPool::Cancellable* newPendingRequest(void* context) PURE;

  // Creates a new connection if allowed by resourceManager, or if created to avoid
  // starving this pool.
  void tryCreateNewConnection();

  virtual void attachRequestToClient(ActiveClient& client, PendingRequest& request) PURE;
  void attachRequestToClientImpl(Envoy::ConnectionPool::ActiveClient& client, void* pair);
  virtual void onPoolFailure(const Upstream::HostDescriptionConstSharedPtr& host_description,
                             absl::string_view failure_reason,
                             ConnectionPool::PoolFailureReason pool_failure_reason,
                             void* context) PURE;
  virtual void onPoolReady(ActiveClient& client, void* context) PURE;
  // Called by derived classes any time a request is completed or destroyed for any reason.
  void onRequestClosed(Envoy::ConnectionPool::ActiveClient& client, bool delay_attaching_request);

  const Upstream::HostConstSharedPtr host_;
  const Upstream::ResourcePriority priority_;

  friend class ActiveClient;
  friend class PendingRequest;
  Event::Dispatcher& dispatcher_;
  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;
  const Network::TransportSocketOptionsSharedPtr transport_socket_options_;

  std::list<Instance::DrainedCb> drained_callbacks_;
  std::list<PendingRequestPtr> pending_requests_;

  // When calling purgePendingRequests, this list will be used to hold the requests we are about
  // to purge. We need this if one cancelled requests cancels a different pending request
  std::list<PendingRequestPtr> pending_requests_to_purge_;

  // Clients that are ready to handle additional requests.
  // All entries are in state READY.
  std::list<ActiveClientPtr> ready_clients_;

  // Clients that are not ready to handle additional requests due to being BUSY or DRAINING.
  std::list<ActiveClientPtr> busy_clients_;

  // Clients that are not ready to handle additional requests because they are CONNECTING.
  std::list<ActiveClientPtr> connecting_clients_;

  // The number of requests currently attached to clients.
  uint64_t num_active_requests_{0};

  // The number of requests that can be immediately dispatched
  // if all CONNECTING connections become connected.
  uint64_t connecting_request_capacity_{0};
};
} // namespace ConnectionPool

namespace Http {

// An implementation of Envoy::ConnectionPool::PendingRequest for HTTP/1.1 and HTTP/2
class HttpPendingRequest : public Envoy::ConnectionPool::PendingRequest {
public:
  // OnPoolSuccess for HTTP requires both the decoder and callbacks. OnPoolFailure
  // requires only the callbacks, but passes both for consistency.
  using AttachContext = std::pair<Http::ResponseDecoder*, Http::ConnectionPool::Callbacks*>;
  HttpPendingRequest(Envoy::ConnectionPool::ConnPoolImplBase& parent,
                     Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
      : Envoy::ConnectionPool::PendingRequest(parent),
        context_(std::make_pair(&decoder, &callbacks)) {}

  void* context() override { return static_cast<void*>(&context_); }
  AttachContext context_;
};

// An implementation of Envoy::ConnectionPool::ConnPoolImplBase for shared code
// between HTTP/1.1 and HTTP/2
class HttpConnPoolImplBase : public Envoy::ConnectionPool::ConnPoolImplBase,
                             public Http::ConnectionPool::Instance {
public:
  using AttachContext = std::pair<Http::ResponseDecoder*, Http::ConnectionPool::Callbacks*>;

  HttpConnPoolImplBase(Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                       Event::Dispatcher& dispatcher,
                       const Network::ConnectionSocket::OptionsSharedPtr& options,
                       const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                       Http::Protocol protocol);

  // ConnectionPool::Instance
  void addDrainedCallback(DrainedCb cb) override { addDrainedCallbackImpl(cb); }
  void drainConnections() override { drainConnectionsImpl(); }
  Upstream::HostDescriptionConstSharedPtr host() const override { return host_; }
  ConnectionPool::Cancellable* newStream(Http::ResponseDecoder& response_decoder,
                                         Http::ConnectionPool::Callbacks& callbacks) override;
  bool hasActiveConnections() const override;

  void attachRequestToClient(Envoy::ConnectionPool::ActiveClient& client,
                             Envoy::ConnectionPool::PendingRequest& base_request) override {
    HttpPendingRequest* request = reinterpret_cast<HttpPendingRequest*>(&base_request);
    attachRequestToClientImpl(client, static_cast<void*>(&request->context_));
  }

  // Creates a new PendingRequest and enqueues it into the request queue.
  ConnectionPool::Cancellable* newPendingRequest(void* context) override;
  void onPoolFailure(const Upstream::HostDescriptionConstSharedPtr& host_description,
                     absl::string_view failure_reason, ConnectionPool::PoolFailureReason reason,
                     void* context) override {
    auto* callbacks = reinterpret_cast<AttachContext*>(context)->second;
    callbacks->onPoolFailure(reason, failure_reason, host_description);
  }
  void onPoolReady(Envoy::ConnectionPool::ActiveClient& client, void* context) override;

  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;
};

// An implementation of Envoy::ConnectionPool::ActiveClient for HTTP/1.1 and HTTP/2
class ActiveClient : public Envoy::ConnectionPool::ActiveClient {
public:
  ActiveClient(HttpConnPoolImplBase& parent, uint64_t lifetime_request_limit,
               uint64_t concurrent_request_limit)
      : Envoy::ConnectionPool::ActiveClient(parent, lifetime_request_limit,
                                            concurrent_request_limit) {
    Upstream::Host::CreateConnectionData data = parent_.host_->createConnection(
        parent_.dispatcher_, parent_.socket_options_, parent_.transport_socket_options_);
    real_host_description_ = data.host_description_;
    codec_client_ = parent.createCodecClient(data);
    codec_client_->addConnectionCallbacks(*this);
    codec_client_->setConnectionStats(
        {parent_.host_->cluster().stats().upstream_cx_rx_bytes_total_,
         parent_.host_->cluster().stats().upstream_cx_rx_bytes_buffered_,
         parent_.host_->cluster().stats().upstream_cx_tx_bytes_total_,
         parent_.host_->cluster().stats().upstream_cx_tx_bytes_buffered_,
         &parent_.host_->cluster().stats().bind_errors_, nullptr});
  }
  void close() override { codec_client_->close(); }
  virtual Http::RequestEncoder& newStreamEncoder(Http::ResponseDecoder& response_decoder) PURE;
  void onEvent(Network::ConnectionEvent event) override {
    parent_.onConnectionEvent(*this, codec_client_->connectionFailureReason(), event);
  }
  size_t numActiveRequests() const override { return codec_client_->numActiveRequests(); }
  uint64_t id() const override { return codec_client_->id(); }

  Http::CodecClientPtr codec_client_;
};

} // namespace Http

} // namespace Envoy
