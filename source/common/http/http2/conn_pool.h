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

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * Implementation of a "connection pool" for HTTP/2. This mainly handles stats as well as
 * shifting to a new connection if we reach max streams on the primary. This is a base class
 * used for both the prod implementation as well as the testing one.
 */
class ConnPoolImpl : Logger::Loggable<Logger::Id::pool>, public ConnectionPool::Instance {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options);
  ~ConnPoolImpl();

  // Http::ConnectionPool::Instance
  Http::Protocol protocol() const override { return Http::Protocol::Http2; }
  void addDrainedCallback(DrainedCb cb) override;
  void drainConnections() override;
  ConnectionPool::Cancellable* newStream(Http::StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) override;

protected:
  struct ActiveClient : public Network::ConnectionCallbacks,
                        public CodecClientCallbacks,
                        public Event::DeferredDeletable,
                        public Http::ConnectionCallbacks {
    ActiveClient(ConnPoolImpl& parent);
    ~ActiveClient();

    void onConnectTimeout() { parent_.onConnectTimeout(*this); }

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
    Event::TimerPtr connect_timer_;
    Stats::TimespanPtr conn_length_;
    bool closed_with_active_rq_{};
  };

  typedef std::unique_ptr<ActiveClient> ActiveClientPtr;

  void checkForDrained();
  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;
  virtual uint32_t maxTotalStreams() PURE;
  void movePrimaryClientToDraining();
  void onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event);
  void onConnectTimeout(ActiveClient& client);
  void onGoAway(ActiveClient& client);
  void onStreamDestroy(ActiveClient& client);
  void onStreamReset(ActiveClient& client, Http::StreamResetReason reason);

  Stats::TimespanPtr conn_connect_ms_;
  Event::Dispatcher& dispatcher_;
  Upstream::HostConstSharedPtr host_;
  ActiveClientPtr primary_client_;
  ActiveClientPtr draining_client_;
  std::list<DrainedCb> drained_callbacks_;
  Upstream::ResourcePriority priority_;
  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;
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
} // namespace Http
} // namespace Envoy
