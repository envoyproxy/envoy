#pragma once

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/event/timer.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/stats/timespan.h"
#include "envoy/upstream/upstream.h"

#include "common/http/codec_client.h"
#include "common/http/conn_pool_base_legacy.h"

namespace Envoy {
namespace Http {
namespace Legacy {
namespace Http2 {

/**
 * Implementation of a "connection pool" for HTTP/2. This mainly handles stats as well as
 * shifting to a new connection if we reach max streams on the primary. This is a base class
 * used for both the prod implementation as well as the testing one.
 */
class ConnPoolImpl : public ConnectionPool::Instance, public Legacy::ConnPoolImplBase {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Network::TransportSocketOptionsSharedPtr& transport_socket_options);
  ~ConnPoolImpl() override;

  // Http::ConnectionPool::Instance
  Http::Protocol protocol() const override { return Http::Protocol::Http2; }
  void addDrainedCallback(DrainedCb cb) override;
  void drainConnections() override;
  bool hasActiveConnections() const override;
  ConnectionPool::Cancellable* newStream(ResponseDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) override;
  Upstream::HostDescriptionConstSharedPtr host() const override { return host_; };

protected:
  struct ActiveClient : ConnPoolImplBase::ActiveClient,
                        public Network::ConnectionCallbacks,
                        public CodecClientCallbacks,
                        public Event::DeferredDeletable,
                        public Http::ConnectionCallbacks {
    ActiveClient(ConnPoolImpl& parent);
    ~ActiveClient() override;

    void onConnectTimeout() override { parent_.onConnectTimeout(*this); }

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override {
      parent_.onConnectionEvent(*this, event);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // CodecClientCallbacks
    void onStreamDestroy() override { parent_.onStreamDestroy(*this); }
    void onStreamReset(Http::StreamResetReason reason) override {
      parent_.onStreamReset(*this, reason);
    }

    // Http::ConnectionCallbacks
    void onGoAway() override { parent_.onGoAway(*this); }

    ConnPoolImpl& parent_;
    CodecClientPtr client_;
    Upstream::HostDescriptionConstSharedPtr real_host_description_;
    uint64_t total_streams_{};
    bool upstream_ready_{};
    bool closed_with_active_rq_{};
  };

  using ActiveClientPtr = std::unique_ptr<ActiveClient>;

  // Http::ConnPoolImplBase
  void checkForDrained() override;

  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;
  virtual uint32_t maxTotalStreams() PURE;
  void movePrimaryClientToDraining();
  void onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event);
  void onConnectTimeout(ActiveClient& client);
  void onGoAway(ActiveClient& client);
  void onStreamDestroy(ActiveClient& client);
  void onStreamReset(ActiveClient& client, Http::StreamResetReason reason);
  void newClientStream(ResponseDecoder& response_decoder, ConnectionPool::Callbacks& callbacks);
  void onUpstreamReady();

  Event::Dispatcher& dispatcher_;
  ActiveClientPtr primary_client_;
  ActiveClientPtr draining_client_;
  std::list<DrainedCb> drained_callbacks_;
  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;
  const Network::TransportSocketOptionsSharedPtr transport_socket_options_;
};

/**
 * Production implementation of the HTTP/2 connection pool.
 */
class ProdConnPoolImpl : public ConnPoolImpl {
public:
  using ConnPoolImpl::ConnPoolImpl;

private:
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;
  uint32_t maxTotalStreams() override;

  // All streams are 2^31. Client streams are half that, minus stream 0. Just to be on the safe
  // side we do 2^29.
  static const uint64_t MAX_STREAMS = (1 << 29);
};

} // namespace Http2
} // namespace Legacy
} // namespace Http
} // namespace Envoy
