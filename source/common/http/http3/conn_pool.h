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

  void onMaxStreamsChanged(uint32_t num_streams);

  RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override {
    ASSERT(quiche_capacity_ != 0);
    updateCapacity(quiche_capacity_ - 1);
    return MultiplexedActiveClientBase::newStreamEncoder(response_decoder);
  }

  int64_t currentUnusedCapacity() const override {
    return std::min<int64_t>(quiche_capacity_, effectiveConcurrentStreamLimit());
  }

  void updateCapacity(uint64_t new_quiche_capacity) {
    uint64_t old_capacity = currentUnusedCapacity();
    quiche_capacity_ = new_quiche_capacity;
    uint64_t new_capacity = currentUnusedCapacity();

    if (new_capacity < old_capacity) {
      parent_.decrClusterStreamCapacity(old_capacity - new_capacity);
    } else if (old_capacity < new_capacity) {
      parent_.incrClusterStreamCapacity(new_capacity - old_capacity);
    }
  }

  std::unique_ptr<Quic::ScopedStreamNotifier> notifier_;
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
  bool quic() override { return true; }

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
