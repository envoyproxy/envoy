#include "source/common/http/http3/conn_pool.h"

#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "source/common/config/utility.h"
#include "source/common/http/http3/quic_client_connection_factory.h"
#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {
namespace Http3 {
namespace {

uint32_t getMaxStreams(const Upstream::ClusterInfo& cluster) {
  return PROTOBUF_GET_WRAPPED_OR_DEFAULT(cluster.http3Options().quic_protocol_options(),
                                         max_concurrent_streams, 100);
}

} // namespace

ActiveClient::ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
                           Upstream::Host::CreateConnectionData& data)
    : MultiplexedActiveClientBase(parent, getMaxStreams(parent.host()->cluster()),
                                  parent.host()->cluster().stats().upstream_cx_http3_total_, data) {
}

void ActiveClient::onMaxStreamsChanged(uint32_t num_streams) {
  updateCapacity(num_streams);
  if (state() == ActiveClient::State::BUSY && currentUnusedCapacity() != 0) {
    parent_.transitionActiveClientState(*this, ActiveClient::State::READY);
    // If there's waiting streams, make sure the pool will now serve them.
    parent_.onUpstreamReady();
  } else if (currentUnusedCapacity() == 0 && state() == ActiveClient::State::READY) {
    // With HTTP/3 this can only happen during a rejected 0-RTT handshake.
    parent_.transitionActiveClientState(*this, ActiveClient::State::BUSY);
  }
}

void Http3ConnPoolImpl::setQuicConfigFromClusterConfig(const Upstream::ClusterInfo& cluster,
                                                       quic::QuicConfig& quic_config) {
  Quic::convertQuicConfig(cluster.http3Options().quic_protocol_options(), quic_config);
  quic::QuicTime::Delta crypto_timeout =
      quic::QuicTime::Delta::FromMilliseconds(cluster.connectTimeout().count());
  quic_config.set_max_time_before_crypto_handshake(crypto_timeout);
}

Http3ConnPoolImpl::Http3ConnPoolImpl(
    Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    Random::RandomGenerator& random_generator, Upstream::ClusterConnectivityState& state,
    CreateClientFn client_fn, CreateCodecFn codec_fn, std::vector<Http::Protocol> protocol,
    TimeSource& time_source)
    : FixedHttpConnPoolImpl(host, priority, dispatcher, options, transport_socket_options,
                            random_generator, state, client_fn, codec_fn, protocol) {
  auto source_address = host_->cluster().sourceAddress();
  if (!source_address.get()) {
    auto host_address = host->address();
    source_address = Network::Utility::getLocalAddress(host_address->ip()->version());
  }
  Network::TransportSocketFactory& transport_socket_factory = host->transportSocketFactory();
  quic::QuicConfig quic_config;
  setQuicConfigFromClusterConfig(host_->cluster(), quic_config);
  quic_info_ = std::make_unique<Quic::PersistentQuicInfoImpl>(
      dispatcher, transport_socket_factory, time_source, source_address, quic_config,
      host->cluster().perConnectionBufferLimitBytes());
}

// Make sure all connections are torn down before quic_info_ is deleted.
Http3ConnPoolImpl::~Http3ConnPoolImpl() { destructAllConnections(); }

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state, TimeSource& time_source,
                 Quic::QuicStatNames& quic_stat_names, Stats::Scope& scope) {
  return std::make_unique<Http3ConnPoolImpl>(
      host, priority, dispatcher, options, transport_socket_options, random_generator, state,
      [&quic_stat_names,
       &scope](HttpConnPoolImplBase* pool) -> ::Envoy::ConnectionPool::ActiveClientPtr {
        ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::pool), debug,
                            "Creating Http/3 client");
        // If there's no ssl context, the secrets are not loaded. Fast-fail by returning null.
        auto factory = &pool->host()->transportSocketFactory();
        ASSERT(dynamic_cast<Quic::QuicClientTransportSocketFactory*>(factory) != nullptr);
        if (static_cast<Quic::QuicClientTransportSocketFactory*>(factory)->sslCtx() == nullptr) {
          ENVOY_LOG_EVERY_POW_2_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::pool),
                                          warn,
                                          "Failed to create Http/3 client. Transport socket "
                                          "factory is not configured correctly.");
          return nullptr;
        }
        Http3ConnPoolImpl* h3_pool = reinterpret_cast<Http3ConnPoolImpl*>(pool);
        Upstream::Host::CreateConnectionData data{};
        data.host_description_ = pool->host();
        auto host_address = data.host_description_->address();
        auto source_address = data.host_description_->cluster().sourceAddress();
        if (!source_address.get()) {
          source_address = Network::Utility::getLocalAddress(host_address->ip()->version());
        }
        data.connection_ =
            Quic::createQuicNetworkConnection(h3_pool->quicInfo(), pool->dispatcher(), host_address,
                                              source_address, quic_stat_names, scope);
        if (data.connection_ == nullptr) {
          ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
              Envoy::Logger::Registry::getLog(Envoy::Logger::Id::pool), warn,
              "Failed to create Http/3 client. Failed to create quic network connection.");
          return nullptr;
        }
        // Store a handle to connection as it will be moved during client construction.
        Network::Connection& connection = *data.connection_;
        auto client = std::make_unique<ActiveClient>(*pool, data);
        if (connection.state() == Network::Connection::State::Closed) {
          return nullptr;
        }
        return client;
      },
      [](Upstream::Host::CreateConnectionData& data, HttpConnPoolImplBase* pool) {
        CodecClientPtr codec{new CodecClientProd(CodecType::HTTP3, std::move(data.connection_),
                                                 data.host_description_, pool->dispatcher(),
                                                 pool->randomGenerator())};
        return codec;
      },
      std::vector<Protocol>{Protocol::Http3}, time_source);
}

} // namespace Http3
} // namespace Http
} // namespace Envoy
