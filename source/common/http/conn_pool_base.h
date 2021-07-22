#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/stats/timespan.h"

#include "source/common/common/linked_object.h"
#include "source/common/conn_pool/conn_pool_base.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

struct HttpAttachContext : public Envoy::ConnectionPool::AttachContext {
  HttpAttachContext(Http::ResponseDecoder* d, Http::ConnectionPool::Callbacks* c)
      : decoder_(d), callbacks_(c) {}
  Http::ResponseDecoder* decoder_;
  Http::ConnectionPool::Callbacks* callbacks_;
};

// An implementation of Envoy::ConnectionPool::PendingStream for HTTP/1.1 and HTTP/2
class HttpPendingStream : public Envoy::ConnectionPool::PendingStream {
public:
  // OnPoolSuccess for HTTP requires both the decoder and callbacks. OnPoolFailure
  // requires only the callbacks, but passes both for consistency.
  HttpPendingStream(Envoy::ConnectionPool::ConnPoolImplBase& parent, Http::ResponseDecoder& decoder,
                    Http::ConnectionPool::Callbacks& callbacks)
      : Envoy::ConnectionPool::PendingStream(parent), context_(&decoder, &callbacks) {}

  Envoy::ConnectionPool::AttachContext& context() override { return context_; }
  HttpAttachContext context_;
};

class ActiveClient;

/* An implementation of Envoy::ConnectionPool::ConnPoolImplBase for shared code
 * between HTTP/1.1 and HTTP/2
 *
 * NOTE: The connection pool does NOT do DNS resolution. It assumes it is being given a numeric IP
 *       address. Higher layer code should handle resolving DNS on error and creating a new pool
 *       bound to a different IP address.
 */
class HttpConnPoolImplBase : public Envoy::ConnectionPool::ConnPoolImplBase,
                             public Http::ConnectionPool::Instance {
public:
  HttpConnPoolImplBase(
      Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
      Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
      Random::RandomGenerator& random_generator, Upstream::ClusterConnectivityState& state,
      std::vector<Http::Protocol> protocols);
  ~HttpConnPoolImplBase() override;

  // Event::DeferredDeletable
  void deleteIsPending() override { deleteIsPendingImpl(); }

  // ConnectionPool::Instance
  void addIdleCallback(IdleCb cb) override { addIdleCallbackImpl(cb); }
  bool isIdle() const override { return isIdleImpl(); }
  void startDrain() override { startDrainImpl(); }
  void drainConnections() override { drainConnectionsImpl(); }
  Upstream::HostDescriptionConstSharedPtr host() const override { return host_; }
  ConnectionPool::Cancellable* newStream(Http::ResponseDecoder& response_decoder,
                                         Http::ConnectionPool::Callbacks& callbacks) override;
  bool maybePreconnect(float ratio) override {
    return Envoy::ConnectionPool::ConnPoolImplBase::maybePreconnect(ratio);
  }
  bool hasActiveConnections() const override;

  // Creates a new PendingStream and enqueues it into the queue.
  ConnectionPool::Cancellable*
  newPendingStream(Envoy::ConnectionPool::AttachContext& context) override;
  void onPoolFailure(const Upstream::HostDescriptionConstSharedPtr& host_description,
                     absl::string_view failure_reason, ConnectionPool::PoolFailureReason reason,
                     Envoy::ConnectionPool::AttachContext& context) override {
    auto* callbacks = typedContext<HttpAttachContext>(context).callbacks_;
    callbacks->onPoolFailure(reason, failure_reason, host_description);
  }
  void onPoolReady(Envoy::ConnectionPool::ActiveClient& client,
                   Envoy::ConnectionPool::AttachContext& context) override;

  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;
  Random::RandomGenerator& randomGenerator() { return random_generator_; }

protected:
  friend class ActiveClient;
  Random::RandomGenerator& random_generator_;
};

// An implementation of Envoy::ConnectionPool::ActiveClient for HTTP/1.1 and HTTP/2
class ActiveClient : public Envoy::ConnectionPool::ActiveClient {
public:
  ActiveClient(HttpConnPoolImplBase& parent, uint32_t lifetime_stream_limit,
               uint32_t concurrent_stream_limit)
      : Envoy::ConnectionPool::ActiveClient(parent, lifetime_stream_limit,
                                            concurrent_stream_limit) {
    // The static cast makes sure we call the base class host() and not
    // HttpConnPoolImplBase::host which is of a different type.
    Upstream::Host::CreateConnectionData data =
        static_cast<Envoy::ConnectionPool::ConnPoolImplBase*>(&parent)->host()->createConnection(
            parent.dispatcher(), parent.socketOptions(), parent.transportSocketOptions());
    initialize(data, parent);
  }

  ActiveClient(HttpConnPoolImplBase& parent, uint64_t lifetime_stream_limit,
               uint64_t concurrent_stream_limit, Upstream::Host::CreateConnectionData& data)
      : Envoy::ConnectionPool::ActiveClient(parent, lifetime_stream_limit,
                                            concurrent_stream_limit) {
    initialize(data, parent);
  }

  void initialize(Upstream::Host::CreateConnectionData& data, HttpConnPoolImplBase& parent) {
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

  absl::optional<Http::Protocol> protocol() const override { return codec_client_->protocol(); }
  void close() override { codec_client_->close(); }
  virtual Http::RequestEncoder& newStreamEncoder(Http::ResponseDecoder& response_decoder) PURE;
  void onEvent(Network::ConnectionEvent event) override {
    parent_.onConnectionEvent(*this, codec_client_->connectionFailureReason(), event);
  }
  uint32_t numActiveStreams() const override { return codec_client_->numActiveRequests(); }
  uint64_t id() const override { return codec_client_->id(); }
  HttpConnPoolImplBase& parent() { return *static_cast<HttpConnPoolImplBase*>(&parent_); }

  Http::CodecClientPtr codec_client_;
};

/* An implementation of Envoy::ConnectionPool::ConnPoolImplBase for HTTP/1 and HTTP/2
 */
class FixedHttpConnPoolImpl : public HttpConnPoolImplBase {
public:
  using CreateClientFn =
      std::function<Envoy::ConnectionPool::ActiveClientPtr(HttpConnPoolImplBase* pool)>;
  using CreateCodecFn = std::function<CodecClientPtr(Upstream::Host::CreateConnectionData& data,
                                                     HttpConnPoolImplBase* pool)>;

  FixedHttpConnPoolImpl(
      Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
      Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
      Random::RandomGenerator& random_generator, Upstream::ClusterConnectivityState& state,
      CreateClientFn client_fn, CreateCodecFn codec_fn, std::vector<Http::Protocol> protocols)
      : HttpConnPoolImplBase(host, priority, dispatcher, options, transport_socket_options,
                             random_generator, state, protocols),
        codec_fn_(codec_fn), client_fn_(client_fn), protocol_(protocols[0]) {
    ASSERT(protocols.size() == 1);
  }

  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override {
    return codec_fn_(data, this);
  }

  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override {
    return client_fn_(this);
  }

  absl::string_view protocolDescription() const override {
    return Utility::getProtocolString(protocol_);
  }

protected:
  const CreateCodecFn codec_fn_;
  const CreateClientFn client_fn_;
  const Http::Protocol protocol_;
};

/**
 * Active client base for HTTP/2 and HTTP/3
 */
class MultiplexedActiveClientBase : public CodecClientCallbacks,
                                    public Http::ConnectionCallbacks,
                                    public Envoy::Http::ActiveClient {
public:
  MultiplexedActiveClientBase(HttpConnPoolImplBase& parent, uint32_t max_concurrent_streams,
                              Stats::Counter& cx_total);
  MultiplexedActiveClientBase(HttpConnPoolImplBase& parent, uint32_t max_concurrent_streams,
                              Stats::Counter& cx_total, Upstream::Host::CreateConnectionData& data);
  ~MultiplexedActiveClientBase() override = default;

  // ConnPoolImpl::ActiveClient
  bool closingWithIncompleteStream() const override;
  RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override;

  // CodecClientCallbacks
  void onStreamDestroy() override;
  void onStreamReset(Http::StreamResetReason reason) override;

  // Http::ConnectionCallbacks
  void onGoAway(Http::GoAwayErrorCode error_code) override;
  void onSettings(ReceivedSettings& settings) override;

  // As this is called once when the stream is closed, it's a good place to
  // update the counter as one stream has been "returned" and the negative
  // capacity should be reduced.
  bool hadNegativeDeltaOnStreamClosed() override {
    int ret = negative_capacity_ != 0;
    if (negative_capacity_ > 0) {
      negative_capacity_--;
    }
    return ret;
  }

  uint64_t negative_capacity_{};

protected:
  MultiplexedActiveClientBase(Envoy::Http::HttpConnPoolImplBase& parent,
                              Upstream::Host::CreateConnectionData& data,
                              uint32_t max_concurrent_streams, Stats::Counter& cx_total);

private:
  bool closed_with_active_rq_{};
};

} // namespace Http

} // namespace Envoy
