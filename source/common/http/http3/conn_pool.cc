#include "common/http/http3/conn_pool.h"

#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "common/config/utility.h"
#include "common/http/http3/quic_client_connection_factory.h"
#include "common/http/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_features.h"

#ifdef ENVOY_ENABLE_QUIC
#include "common/quic/client_connection_factory_impl.h"
#else
#error "http3 conn pool should not be built with QUIC disabled"
#endif

namespace Envoy {
namespace Http {
namespace Http3 {

// Http3 subclass of FixedHttpConnPoolImpl which exists to store quic data.
class Http3ConnPoolImpl : public FixedHttpConnPoolImpl {
public:
  Http3ConnPoolImpl(Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                    Event::Dispatcher& dispatcher,
                    const Network::ConnectionSocket::OptionsSharedPtr& options,
                    const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                    Random::RandomGenerator& random_generator,
                    Upstream::ClusterConnectivityState& state, CreateClientFn client_fn,
                    CreateCodecFn codec_fn, std::vector<Http::Protocol> protocol,
                    TimeSource& time_source)
      : FixedHttpConnPoolImpl(host, priority, dispatcher, options, transport_socket_options,
                              random_generator, state, client_fn, codec_fn, protocol) {
    auto source_address = host_->cluster().sourceAddress();
    if (!source_address.get()) {
      auto host_address = host->address();
      source_address = Network::Utility::getLocalAddress(host_address->ip()->version());
    }
    Network::TransportSocketFactory& transport_socket_factory = host->transportSocketFactory();
    quic_info_ = std::make_unique<Quic::PersistentQuicInfoImpl>(
        dispatcher, transport_socket_factory, host->cluster().statsScope(), time_source,
        source_address);
  }

  // Make sure all connections are torn down before quic_info_ is deleted.
  ~Http3ConnPoolImpl() override { destructAllConnections(); }

  // Store quic helpers which can be shared between connections and must live
  // beyond the lifetime of individual connections.
  std::unique_ptr<PersistentQuicInfo> quic_info_;
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state, TimeSource& time_source) {
  return std::make_unique<Http3ConnPoolImpl>(
      host, priority, dispatcher, options, transport_socket_options, random_generator, state,
      [](HttpConnPoolImplBase* pool) {
        Http3ConnPoolImpl* h3_pool = reinterpret_cast<Http3ConnPoolImpl*>(pool);
        Upstream::Host::CreateConnectionData data{};
        data.host_description_ = pool->host();
        auto host_address = data.host_description_->address();
        auto source_address = data.host_description_->cluster().sourceAddress();
        if (!source_address.get()) {
          source_address = Network::Utility::getLocalAddress(host_address->ip()->version());
        }
        data.connection_ = Quic::createQuicNetworkConnection(
            *h3_pool->quic_info_, pool->dispatcher(), host_address, source_address);
        return std::make_unique<ActiveClient>(*pool, data);
      },
      [](Upstream::Host::CreateConnectionData& data, HttpConnPoolImplBase* pool) {
        CodecClientPtr codec{new CodecClientProd(
            CodecClient::Type::HTTP3, std::move(data.connection_), data.host_description_,
            pool->dispatcher(), pool->randomGenerator())};
        return codec;
      },
      std::vector<Protocol>{Protocol::Http3}, time_source);
}

} // namespace Http3
} // namespace Http
} // namespace Envoy
