#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/optref.h"
#include "envoy/http/persistent_quic_info.h"
#include "envoy/upstream/upstream.h"

#include "source/common/http/codec_client.h"
#include "source/common/http/conn_pool_base.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_connection_factory_impl.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_transport_socket_factory.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#else
#error "http3 conn pool should not be built with QUIC disabled"
#endif

namespace Envoy {
namespace Http {
namespace Http3 {

class ActiveClient : public MultiplexedActiveClientBase {
public:
  ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
               Upstream::Host::CreateConnectionData& data);

  ~ActiveClient() override {
    if (async_connect_callback_ != nullptr && async_connect_callback_->enabled()) {
      async_connect_callback_->cancel();
    }
  }
  // Http::ConnectionCallbacks
  void onMaxStreamsChanged(uint32_t num_streams) override;

  RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override {
    ASSERT(quiche_capacity_ != 0);
    has_created_stream_ = true;
    // Each time a quic stream is allocated the quic capacity needs to get
    // decremented. See comments by quiche_capacity_.
    updateCapacity(quiche_capacity_ - 1);
    return MultiplexedActiveClientBase::newStreamEncoder(response_decoder);
  }

  uint32_t effectiveConcurrentStreamLimit() const override {
    return std::min<int64_t>(MultiplexedActiveClientBase::effectiveConcurrentStreamLimit(),
                             quiche_capacity_);
  }

  // Overload the default capacity calculations to return the quic capacity
  // (modified by any stream limits in Envoy config)
  int64_t currentUnusedCapacity() const override {
    return std::min<int64_t>(quiche_capacity_, effectiveConcurrentStreamLimit());
  }

  // Overridden to return true as long as the client is doing handshake even when it is ready for
  // early data streams.
  bool hasHandshakeCompleted() const override { return has_handshake_completed_; }

  // Overridden to include ReadyForEarlyData state.
  bool readyForStream() const override {
    return state() == State::Ready || state() == State::ReadyForEarlyData;
  }

  void updateCapacity(uint64_t new_quiche_capacity) {
    // Each time we update the capacity make sure to reflect the update in the
    // connection pool.
    //
    // Due to interplay between the max number of concurrent streams Envoy will
    // allow and the max number of streams per connection this is not as simple
    // as just updating based on the delta between quiche_capacity_ and
    // new_quiche_capacity, so we use the delta between the actual calculated
    // capacity before and after the update.
    uint64_t old_capacity = currentUnusedCapacity();
    quiche_capacity_ = new_quiche_capacity;
    uint64_t new_capacity = currentUnusedCapacity();

    if (new_capacity < old_capacity) {
      parent_.decrConnectingAndConnectedStreamCapacity(old_capacity - new_capacity, *this);
    } else if (old_capacity < new_capacity) {
      parent_.incrConnectingAndConnectedStreamCapacity(new_capacity - old_capacity, *this);
    }
  }

  bool hasCreatedStream() const { return has_created_stream_; }

protected:
  bool supportsEarlyData() const override { return true; }

private:
  // Unlike HTTP/2 and HTTP/1, rather than having a cap on the number of active
  // streams, QUIC has a fixed number of streams available which is updated via
  // the MAX_STREAMS frame.
  //
  // As such each time we create a new stream for QUIC, the capacity goes down
  // by one, but unlike the other two codecs it is _not_ restored on stream
  // closure.
  //
  // We track the QUIC capacity here, and overload currentUnusedCapacity so the
  // connection pool can accurately keep track of when it is safe to create new
  // streams.
  //
  // Though HTTP/3 should arguably start out with 0 stream capacity until the
  // initial handshake is complete and MAX_STREAMS frame has been received,
  // assume optimistically it will get ~100 streams, so that the connection pool
  // won't fetch a connection for each incoming stream but will assume that the
  // first connection will likely be able to serve 100.
  // This number will be updated to the correct value before the connection is
  // deemed connected, at which point further connections will be established if
  // necessary.
  uint64_t quiche_capacity_ = 100;
  // Used to schedule a deferred connect() call. Because HTTP/3 codec client can
  // do 0-RTT during connect(), deferring it to avoid handling network events during CodecClient
  // construction.
  Event::SchedulableCallbackPtr async_connect_callback_;
  // True if newStream() is ever called.
  bool has_created_stream_{false};
};

// An interface to propagate H3 handshake result.
// TODO(danzh) add an API to propagate 0-RTT handshake failure.
class PoolConnectResultCallback {
public:
  virtual ~PoolConnectResultCallback() = default;

  // Called when the mandatory handshake is complete. This is when a HTTP/3 connection is regarded
  // as connected and is able to send requests.
  virtual void onHandshakeComplete() PURE;
  // Called upon connection close event from a client who hasn't finish handshake but already sent
  // early data.
  // TODO(danzh) actually call it from h3 pool.
  virtual void onZeroRttHandshakeFailed() PURE;
};

// Http3 subclass of FixedHttpConnPoolImpl which exists to store quic data.
class Http3ConnPoolImpl : public FixedHttpConnPoolImpl {
public:
  Http3ConnPoolImpl(Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                    Event::Dispatcher& dispatcher,
                    const Network::ConnectionSocket::OptionsSharedPtr& options,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    Random::RandomGenerator& random_generator,
                    Upstream::ClusterConnectivityState& state, CreateClientFn client_fn,
                    CreateCodecFn codec_fn, std::vector<Http::Protocol> protocol,
                    OptRef<PoolConnectResultCallback> connect_callback,
                    Http::PersistentQuicInfo& quic_info);

  ~Http3ConnPoolImpl() override;
  ConnectionPool::Cancellable* newStream(Http::ResponseDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks,
                                         const Instance::StreamOptions& options) override;

  // For HTTP/3 the base connection pool does not track stream capacity, rather
  // the HTTP3 active client does.
  bool trackStreamCapacity() override { return false; }

  std::unique_ptr<Network::ClientConnection>
  createClientConnection(Quic::QuicStatNames& quic_stat_names,
                         OptRef<Http::HttpServerPropertiesCache> rtt_cache, Stats::Scope& scope);

protected:
  void onConnected(Envoy::ConnectionPool::ActiveClient&) override;
  void onConnectFailed(Envoy::ConnectionPool::ActiveClient&) override;

private:
  friend class Http3ConnPoolImplPeer;

  // Latches Quic helpers shared across the cluster
  Quic::PersistentQuicInfoImpl& quic_info_;
  // server-id can change over the lifetime of Envoy but will be consistent for a
  // given connection pool.
  quic::QuicServerId server_id_;
  // If not nullopt, called when the handshake state changes.
  OptRef<PoolConnectResultCallback> connect_callback_;

  quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
};

std::unique_ptr<Http3ConnPoolImpl>
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state, Quic::QuicStatNames& quic_stat_names,
                 OptRef<Http::HttpServerPropertiesCache> rtt_cache, Stats::Scope& scope,
                 OptRef<PoolConnectResultCallback> connect_callback,
                 Http::PersistentQuicInfo& quic_info);

} // namespace Http3
} // namespace Http
} // namespace Envoy
