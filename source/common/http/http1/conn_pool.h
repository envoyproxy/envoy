#pragma once

#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/upstream/upstream.h"

#include "source/common/http/codec_wrappers.h"
#include "source/common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * An active client for HTTP/1.1 connections.
 */
class ActiveClient : public Envoy::Http::ActiveClient {
public:
  ActiveClient(HttpConnPoolImplBase& parent, OptRef<Upstream::Host::CreateConnectionData> data);
  ~ActiveClient() override;

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
  void releaseResources() override {
    parent_.dispatcher().deferredDelete(std::move(stream_wrapper_));
    Envoy::Http::ActiveClient::releaseResources();
  }

  struct StreamWrapper : public RequestEncoderWrapper,
                         public ResponseDecoderWrapper,
                         public StreamCallbacks,
                         public Event::DeferredDeletable,
                         protected Logger::Loggable<Logger::Id::pool> {
  public:
    StreamWrapper(ResponseDecoder& response_decoder, ActiveClient& parent);
    ~StreamWrapper() override;

    // StreamEncoderWrapper
    void onEncodeComplete() override;

    // StreamDecoderWrapper
    void decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void onPreDecodeComplete() override {}
    void onDecodeComplete() override;

    // Http::StreamCallbacks
    void onResetStream(StreamResetReason, absl::string_view) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ActiveClient& parent_;
    bool stream_incomplete_{};
    bool encode_complete_{};
    bool decode_complete_{};
    bool close_connection_{};
  };
  using StreamWrapperPtr = std::unique_ptr<StreamWrapper>;

  StreamWrapperPtr stream_wrapper_;
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state);

} // namespace Http1
} // namespace Http
} // namespace Envoy
