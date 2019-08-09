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
#include "common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * Implementation of a "connection pool" for HTTP/2. This mainly handles stats as well as
 * shifting to a new connection if we reach max streams on the primary. This is a base class
 * used for both the prod implementation as well as the testing one.
 */
class ConnPoolImpl : public ConnectionPool::Instance, public ConnPoolImplBase {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options);
  ~ConnPoolImpl() override;

  // Http::ConnectionPool::Instance
  Http::Protocol protocol() const override { return Http::Protocol::Http2; }
  void addDrainedCallback(DrainedCb cb) override;
  void drainConnections() override;
  bool hasActiveConnections() const override;
  Upstream::HostDescriptionConstSharedPtr host() const override { return host_; };
  const Upstream::ResourcePriority& resourcePriority() const { return priority_; };

protected:
  struct ActiveClient : LinkedObject<ActiveClient>,
                        public Network::ConnectionCallbacks,
                        public CodecClientCallbacks,
                        public Event::DeferredDeletable,
                        public Http::ConnectionCallbacks,
                        public Upstream::ConnectionRequestPolicySubscriber {
    ActiveClient(ConnPoolImpl& parent);
    ~ActiveClient() override;

    void onConnectTimeout() { parent_.onConnectTimeout(*this); }
    void onIdleTimeout() { parent_.onIdleTimeout(*this); }

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

    // Upstream::ConnectionRequestPolicySubscriber
    uint64_t requestCount() const override { return total_streams_; };
    Upstream::ResourceManager& resourceManager() const override;

    ConnPoolImpl& parent_;
    CodecClientPtr client_;
    Upstream::HostDescriptionConstSharedPtr real_host_description_;
    uint64_t total_streams_{};
    Event::TimerPtr connect_timer_;
    Event::TimerPtr idle_timer_;
    bool upstream_ready_{};
    Stats::TimespanPtr conn_length_;
    bool closed_with_active_rq_{};
    Upstream::ConnectionRequestPolicy::State state_;
    bool remote_closed_ = {false};
  };

  using ActiveClientPtr = std::unique_ptr<ActiveClient>;

  // Http::ConnPoolImplBase
  void checkForDrained() override;

  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;
  virtual uint32_t maxTotalStreams() PURE;
  /*
  void movePrimaryClientToDraining();
  */
  void onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event);
  void onConnectTimeout(ActiveClient& client);
  void onIdleTimeout(ActiveClient& client);
  void onGoAway(ActiveClient& client);
  void onStreamDestroy(ActiveClient& client);
  void onStreamReset(ActiveClient& client, Http::StreamResetReason reason);
  void attachRequestToClient(ActiveClient& client, Http::StreamDecoder& response_decoder,
                             ConnectionPool::Callbacks& callbacks);
  void createNewConnection();
  void onUpstreamReady(ActiveClient& client);
  ConnectionPool::Cancellable* newStream(Http::StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) override;

  void applyToEachClient(std::list<ActiveClientPtr>& client_list,
                         const std::function<void(const ActiveClientPtr&)>& fn);
  Stats::TimespanPtr conn_connect_ms_;
  Event::Dispatcher& dispatcher_;

  // Connecting clients list. Connections remain in the list till either:
  //   - Connection is established
  //   - Connection time out
  //   - Connection terminated (by remote)
  std::list<ActiveClientPtr> connecting_clients_;

  // Connected clients waiting for requests. Connections remain in this list
  // till:
  //  - Request is attached
  //  - Connection terminated
  std::list<ActiveClientPtr> ready_clients_;

  // Connections serving at least one request. Connections remain in this list
  // till:
  //  - Max requests per connection is exceeded
  //  - Connection terminated
  std::list<ActiveClientPtr> busy_clients_;

  // Connections with number of requests >= max requests per connection. Connections
  // remain in this list till:
  //  - Excess requests in the connections finish.
  //  - Connection terminated
  std::list<ActiveClientPtr> overflow_clients_;

  // Connections that have active requests but can no longer accept new
  // requests. Depending on the connection policy, these connections could be moved to
  // `to_close_clients` list. Connections could also be moved to this list upon:
  //    - timeout.
  //    - if requests exceed the max allowed by connection policy.
  std::list<ActiveClientPtr> drain_clients_;

  // Connections that are waiting to be closed. Connections are moved to this
  // list when drain clients does not have any more requests being served.
  // Connections remain in this list till:
  //  - Connection is closed.
  std::list<ActiveClientPtr> to_close_clients_;
  std::list<DrainedCb> drained_callbacks_;
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
