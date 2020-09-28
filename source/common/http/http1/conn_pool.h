#pragma once

#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/upstream/upstream.h"

#include "common/http/codec_wrappers.h"
#include "common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * A connection pool implementation for HTTP/1.1 connections.
 * NOTE: The connection pool does NOT do DNS resolution. It assumes it is being given a numeric IP
 *       address. Higher layer code should handle resolving DNS on error and creating a new pool
 *       bound to a different IP address.
 */
class ConnPoolImpl : public Http::HttpConnPoolImplBase {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
               Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

  ~ConnPoolImpl() override;

  // ConnectionPool::Instance
  Http::Protocol protocol() const override { return Http::Protocol::Http11; }

  // ConnPoolImplBase
  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override;

protected:
  class ActiveClient;

  struct StreamWrapper : public RequestEncoderWrapper,
                         public ResponseDecoderWrapper,
                         public StreamCallbacks {
    StreamWrapper(ResponseDecoder& response_decoder, ActiveClient& parent);
    ~StreamWrapper() override;

    // StreamEncoderWrapper
    void onEncodeComplete() override;

    // StreamDecoderWrapper
    void decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void onPreDecodeComplete() override {}
    void onDecodeComplete() override;

    // Http::StreamCallbacks
    void onResetStream(StreamResetReason, absl::string_view) override {
      parent_.parent().onDownstreamReset(parent_);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ActiveClient& parent_;
    bool encode_complete_{};
    bool close_connection_{};
    bool decode_complete_{};
  };

  using StreamWrapperPtr = std::unique_ptr<StreamWrapper>;

  class ActiveClient : public Envoy::Http::ActiveClient {
  public:
    ActiveClient(ConnPoolImpl& parent);

    ConnPoolImpl& parent() { return static_cast<ConnPoolImpl&>(parent_); }

    // ConnPoolImplBase::ActiveClient
    bool closingWithIncompleteStream() const override;
    RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override;

    StreamWrapperPtr stream_wrapper_;
  };

  void onDownstreamReset(ActiveClient& client);
  void onResponseComplete(ActiveClient& client);

  Event::SchedulableCallbackPtr upstream_ready_cb_;
  bool upstream_ready_enabled_{false};
  Random::RandomGenerator& random_generator_;
};

/**
 * Production implementation of the ConnPoolImpl.
 */
class ProdConnPoolImpl : public ConnPoolImpl {
public:
  using ConnPoolImpl::ConnPoolImpl;

  // ConnPoolImpl
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

} // namespace Http1
} // namespace Http
} // namespace Envoy
