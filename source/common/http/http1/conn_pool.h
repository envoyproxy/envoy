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
               const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
               Upstream::ClusterConnectivityState& state);

  ~ConnPoolImpl() override;

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
      parent_.codec_client_->close();
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    void onStreamDestroy();

    ActiveClient& parent_;
    bool stream_incomplete_{};
    bool encode_complete_{};
    bool decode_complete_{};
    bool close_connection_{};
  };

  using StreamWrapperPtr = std::unique_ptr<StreamWrapper>;

  class ActiveClient : public Envoy::Http::ActiveClient {
  public:
    ActiveClient(ConnPoolImpl& parent);

    ConnPoolImpl& parent() { return *static_cast<ConnPoolImpl*>(&parent_); }

    // ConnPoolImplBase::ActiveClient
    bool closingWithIncompleteStream() const override;
    RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override;

    uint32_t numActiveStreams() const override {
      // Override the parent class using the codec for numActiveStreams.
      // Unfortunately for the HTTP/1 codec, the stream is destroyed before decode
      // is complete, and we must make sure the connection pool does not observe available
      // capacity and assign a new stream before decode is complete.
      return stream_wrapper_.get() ? 1 : 0;
    }
    StreamWrapperPtr stream_wrapper_;
  };
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
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state);

} // namespace Http1
} // namespace Http
} // namespace Envoy
