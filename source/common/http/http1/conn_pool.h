#pragma once

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/upstream.h"

#include "common/common/linked_object.h"
#include "common/http/codec_wrappers.h"
#include "common/http/conn_pool_base.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * A connection pool implementation for HTTP/1.1 connections.
 * NOTE: The connection pool does NOT do DNS resolution. It assumes it is being given a numeric IP
 *       address. Higher layer code should handle resolving DNS on error and creating a new pool
 *       bound to a different IP address.
 */
class ConnPoolImpl : public ConnPoolImplBase {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Http::Http1Settings& settings,
               const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

  ~ConnPoolImpl() override;

  // ConnectionPool::Instance
  Http::Protocol protocol() const override { return Http::Protocol::Http11; }
  ConnectionPool::Cancellable* newStream(StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) override;
  Upstream::HostDescriptionConstSharedPtr host() const override { return host_; };

protected:
  struct ActiveClient;

  struct StreamWrapper : public StreamEncoderWrapper,
                         public StreamDecoderWrapper,
                         public StreamCallbacks {
    StreamWrapper(StreamDecoder& response_decoder, ActiveClient& parent);
    ~StreamWrapper() override;

    // StreamEncoderWrapper
    void onEncodeComplete() override;

    // StreamDecoderWrapper
    void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void onPreDecodeComplete() override {}
    void onDecodeComplete() override;

    // Http::StreamCallbacks
    void onResetStream(StreamResetReason, absl::string_view) override {
      parent_.parent_.onDownstreamReset(parent_);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ActiveClient& parent_;
    bool encode_complete_{};
    bool close_connection_{};
    bool decode_complete_{};
  };

  using StreamWrapperPtr = std::unique_ptr<StreamWrapper>;

  struct ActiveClient : public ConnPoolImplBase::ActiveClient {
    ActiveClient(ConnPoolImpl& parent);
    ~ActiveClient() override;

    // ConnPoolImplBase::ActiveClient
    bool hasActiveRequests() const override { return stream_wrapper_ != nullptr; }
    bool closingWithIncompleteRequest() const override {
      return (stream_wrapper_ != nullptr) && (!stream_wrapper_->decode_complete_);
    }

    ConnPoolImpl& parent_;
    StreamWrapperPtr stream_wrapper_;
    uint64_t remaining_requests_;
  };

  void attachRequestToClient(ActiveClient& client, StreamDecoder& response_decoder,
                             ConnectionPool::Callbacks& callbacks);
  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;
  void createNewConnection();
  void onDownstreamReset(ActiveClient& client);
  void onResponseComplete(ActiveClient& client);
  void onUpstreamReady();
  void processIdleClient(ActiveClient& client, bool delay);
  ActiveClient& firstReady() const { return static_cast<ActiveClient&>(*ready_clients_.front()); }
  ActiveClient& firstBusy() const { return static_cast<ActiveClient&>(*busy_clients_.front()); }

  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;
  const Network::TransportSocketOptionsSharedPtr transport_socket_options_;
  Event::TimerPtr upstream_ready_timer_;
  bool upstream_ready_enabled_{false};
  const Http1Settings settings_;
};

/**
 * Production implementation of the ConnPoolImpl.
 */
class ProdConnPoolImpl : public ConnPoolImpl {
public:
  ProdConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                   Upstream::ResourcePriority priority,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Http::Http1Settings& settings,
                   const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
      : ConnPoolImpl(dispatcher, host, priority, options, settings, transport_socket_options) {}

  // ConnPoolImpl
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
