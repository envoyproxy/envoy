#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/stats/timespan.h"

#include "common/common/linked_object.h"
#include "common/conn_pool/conn_pool_base.h"
#include "common/http/codec_client.h"

#include "absl/strings/string_view.h"

namespace Envoy {
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
