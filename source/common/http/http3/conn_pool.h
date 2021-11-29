#pragma once

#include <cstdint>

#include "envoy/upstream/upstream.h"

#include "source/common/http/codec_client.h"
#include "source/common/http/conn_pool_base.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_connection_factory_impl.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_transport_socket_factory.h"
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

  // Http::ConnectionCallbacks
  void onMaxStreamsChanged(uint32_t num_streams) override;

  RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override {
    ASSERT(quiche_capacity_ != 0);
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

    if (connect_timer_) {
      if (new_capacity < old_capacity) {
        parent_.decrConnectingAndConnectedStreamCapacity(old_capacity - new_capacity);
      } else if (old_capacity < new_capacity) {
        parent_.incrConnectingAndConnectedStreamCapacity(new_capacity - old_capacity);
      }
    } else {
      if (new_capacity < old_capacity) {
        parent_.decrClusterStreamCapacity(old_capacity - new_capacity);
      } else if (old_capacity < new_capacity) {
        parent_.incrClusterStreamCapacity(new_capacity - old_capacity);
      }
    }
  }

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
                    TimeSource& time_source);

  ~Http3ConnPoolImpl() override;

  // Set relevant fields in quic_config based on the cluster configuration
  // supplied in cluster.
  static void setQuicConfigFromClusterConfig(const Upstream::ClusterInfo& cluster,
                                             quic::QuicConfig& quic_config);

  Quic::PersistentQuicInfoImpl& quicInfo() { return *quic_info_; }
  // For HTTP/3 the base connection pool does not track stream capacity, rather
  // the HTTP3 active client does.
  bool trackStreamCapacity() override { return false; }

private:
  // Store quic helpers which can be shared between connections and must live
  // beyond the lifetime of individual connections.
  std::unique_ptr<Quic::PersistentQuicInfoImpl> quic_info_;
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state, TimeSource& time_source,
                 Quic::QuicStatNames& quic_stat_names, Stats::Scope& scope);

} // namespace Http3
} // namespace Http
} // namespace Envoy
