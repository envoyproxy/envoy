#pragma once

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/stats/timespan.h"
#include "envoy/upstream/upstream.h"

#include "common/common/linked_object.h"
#include "common/http/codec_client.h"
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
class ConnPoolImpl : public ConnectionPool::Instance, public ConnPoolImplBase {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options);

  ~ConnPoolImpl();

  // ConnectionPool::Instance
  Http::Protocol protocol() const override { return Http::Protocol::Http11; }
  void addDrainedCallback(DrainedCb cb) override;
  void drainConnections() override;
  ConnectionPool::Cancellable* newStream(StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) override;

  // ConnPoolImplBase
  void checkForDrained() override;

protected:
  struct ActiveClient;

  struct StreamWrapper : public StreamEncoderWrapper,
                         public StreamDecoderWrapper,
                         public StreamCallbacks {
    StreamWrapper(StreamDecoder& response_decoder, ActiveClient& parent);
    ~StreamWrapper();

    // StreamEncoderWrapper
    void onEncodeComplete() override;

    // StreamDecoderWrapper
    void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void onPreDecodeComplete() override {}
    void onDecodeComplete() override;

    // Http::StreamCallbacks
    void onResetStream(StreamResetReason) override { parent_.parent_.onDownstreamReset(parent_); }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ActiveClient& parent_;
    bool encode_complete_{};
    bool saw_close_header_{};
    bool decode_complete_{};
  };

  typedef std::unique_ptr<StreamWrapper> StreamWrapperPtr;

  struct ActiveClient : LinkedObject<ActiveClient>,
                        public Network::ConnectionCallbacks,
                        public Event::DeferredDeletable {
    ActiveClient(ConnPoolImpl& parent);
    ~ActiveClient();

    void onConnectTimeout();

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override {
      parent_.onConnectionEvent(*this, event);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ConnPoolImpl& parent_;
    CodecClientPtr codec_client_;
    Upstream::HostDescriptionConstSharedPtr real_host_description_;
    StreamWrapperPtr stream_wrapper_;
    Event::TimerPtr connect_timer_;
    Stats::TimespanPtr conn_length_;
    uint64_t remaining_requests_;
  };

  typedef std::unique_ptr<ActiveClient> ActiveClientPtr;

  void attachRequestToClient(ActiveClient& client, StreamDecoder& response_decoder,
                             ConnectionPool::Callbacks& callbacks);
  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;
  void createNewConnection();
  void onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event);
  void onDownstreamReset(ActiveClient& client);
  void onResponseComplete(ActiveClient& client);
  void onUpstreamReady();
  void processIdleClient(ActiveClient& client, bool delay);

  Stats::TimespanPtr conn_connect_ms_;
  Event::Dispatcher& dispatcher_;
  std::list<ActiveClientPtr> ready_clients_;
  std::list<ActiveClientPtr> busy_clients_;
  std::list<DrainedCb> drained_callbacks_;
  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;
  Event::TimerPtr upstream_ready_timer_;
  bool upstream_ready_enabled_{false};
};

/**
 * Production implementation of the ConnPoolImpl.
 */
class ConnPoolImplProd : public ConnPoolImpl {
public:
  ConnPoolImplProd(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                   Upstream::ResourcePriority priority,
                   const Network::ConnectionSocket::OptionsSharedPtr& options)
      : ConnPoolImpl(dispatcher, host, priority, options) {}

  // ConnPoolImpl
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
