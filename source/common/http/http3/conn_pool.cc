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

namespace Envoy {
namespace Http {
namespace Http3 {

void setQuicConfigFromClusterConfig(const Upstream::ClusterInfo& cluster,
                                    quic::QuicConfig& quic_config) {
  quic::QuicTime::Delta crypto_timeout =
      quic::QuicTime::Delta::FromMilliseconds(cluster.connectTimeout().count());
  quic_config.set_max_time_before_crypto_handshake(crypto_timeout);
  int32_t max_streams =
      cluster.http3Options().quic_protocol_options().max_concurrent_streams().value();
  quic_config.SetMaxBidirectionalStreamsToSend(max_streams);
  quic_config.SetMaxUnidirectionalStreamsToSend(max_streams);
  Quic::configQuicInitialFlowControlWindow(cluster.http3Options().quic_protocol_options(),
                                           quic_config);
}

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
            h3_pool->quicInfo(), pool->dispatcher(), host_address, source_address);
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
