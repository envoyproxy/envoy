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

struct HttpAttachContext : public Envoy::ConnectionPool::AttachContext {
  HttpAttachContext(Http::ResponseDecoder* d, Http::ConnectionPool::Callbacks* c)
      : decoder_(d), callbacks_(c) {}
  Http::ResponseDecoder* decoder_;
  Http::ConnectionPool::Callbacks* callbacks_;
};

// An implementation of Envoy::ConnectionPool::PendingRequest for HTTP/1.1 and HTTP/2
class HttpPendingRequest : public Envoy::ConnectionPool::PendingRequest {
public:
  // OnPoolSuccess for HTTP requires both the decoder and callbacks. OnPoolFailure
  // requires only the callbacks, but passes both for consistency.
  HttpPendingRequest(Envoy::ConnectionPool::ConnPoolImplBase& parent,
                     Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
      : Envoy::ConnectionPool::PendingRequest(parent), context_(&decoder, &callbacks) {}

  Envoy::ConnectionPool::AttachContext& context() override { return context_; }
  HttpAttachContext context_;
};

// An implementation of Envoy::ConnectionPool::ConnPoolImplBase for shared code
// between HTTP/1.1 and HTTP/2
class HttpConnPoolImplBase : public Envoy::ConnectionPool::ConnPoolImplBase,
                             public Http::ConnectionPool::Instance {
public:
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

  // Creates a new PendingRequest and enqueues it into the request queue.
  ConnectionPool::Cancellable*
  newPendingRequest(Envoy::ConnectionPool::AttachContext& context) override;
  void onPoolFailure(const Upstream::HostDescriptionConstSharedPtr& host_description,
                     absl::string_view failure_reason, ConnectionPool::PoolFailureReason reason,
                     Envoy::ConnectionPool::AttachContext& context) override {
    auto* callbacks = typedContext<HttpAttachContext>(context).callbacks_;
    callbacks->onPoolFailure(reason, failure_reason, host_description);
  }
  void onPoolReady(Envoy::ConnectionPool::ActiveClient& client,
                   Envoy::ConnectionPool::AttachContext& context) override;

  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;
};

// An implementation of Envoy::ConnectionPool::ActiveClient for HTTP/1.1 and HTTP/2
class ActiveClient : public Envoy::ConnectionPool::ActiveClient {
public:
  ActiveClient(HttpConnPoolImplBase& parent, uint64_t lifetime_request_limit,
               uint64_t concurrent_request_limit)
      : Envoy::ConnectionPool::ActiveClient(parent, lifetime_request_limit,
                                            concurrent_request_limit) {
    Upstream::Host::CreateConnectionData data = parent_.host()->createConnection(
        parent_.dispatcher(), parent_.socketOptions(), parent_.transportSocketOptions());
    real_host_description_ = data.host_description_;
    codec_client_ = parent.createCodecClient(data);
    codec_client_->addConnectionCallbacks(*this);
    codec_client_->setConnectionStats(
        {parent_.host()->cluster().stats().upstream_cx_rx_bytes_total_,
         parent_.host()->cluster().stats().upstream_cx_rx_bytes_buffered_,
         parent_.host()->cluster().stats().upstream_cx_tx_bytes_total_,
         parent_.host()->cluster().stats().upstream_cx_tx_bytes_buffered_,
         &parent_.host()->cluster().stats().bind_errors_, nullptr});
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
