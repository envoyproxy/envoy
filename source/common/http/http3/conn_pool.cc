#include "source/common/http/http3/conn_pool.h"

#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "source/common/config/utility.h"
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

void ActiveClient::getReady() {
  MultiplexedActiveClientBase::getReady();
  ASSERT(codec_client_);
  codec_client_->connect();
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

const Envoy::Ssl::ClientContextConfig&
getConfig(Network::TransportSocketFactory& transport_socket_factory) {
  auto* quic_socket_factory =
      dynamic_cast<Quic::QuicClientTransportSocketFactory*>(&transport_socket_factory);
  ASSERT(quic_socket_factory != nullptr);
  return quic_socket_factory->clientContextConfig();
}

Http3ConnPoolImpl::Http3ConnPoolImpl(
    Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    Random::RandomGenerator& random_generator, Upstream::ClusterConnectivityState& state,
    CreateClientFn client_fn, CreateCodecFn codec_fn, std::vector<Http::Protocol> protocol,
    OptRef<PoolConnectResultCallback> connect_callback, Http::PersistentQuicInfo& quic_info)
    : FixedHttpConnPoolImpl(host, priority, dispatcher, options, transport_socket_options,
                            random_generator, state, client_fn, codec_fn, protocol),
      quic_info_(dynamic_cast<Quic::PersistentQuicInfoImpl&>(quic_info)),
      server_id_(getConfig(host_->transportSocketFactory()).serverNameIndication(),
                 static_cast<uint16_t>(host_->address()->ip()->port()), false),
      connect_callback_(connect_callback) {}

void Http3ConnPoolImpl::onConnected(Envoy::ConnectionPool::ActiveClient&) {
  if (connect_callback_ != absl::nullopt) {
    connect_callback_->onHandshakeComplete();
  }
}

// Make sure all connections are torn down before quic_info_ is deleted.
Http3ConnPoolImpl::~Http3ConnPoolImpl() { destructAllConnections(); }

std::shared_ptr<quic::QuicCryptoClientConfig> Http3ConnPoolImpl::cryptoConfig() {
  auto* quic_socket_factory =
      dynamic_cast<Quic::QuicClientTransportSocketFactory*>(&host_->transportSocketFactory());
  ASSERT(quic_socket_factory != nullptr);
  auto context = quic_socket_factory->sslCtx();
  // If the secrets haven't been loaded, there is no crypto config.
  if (context == nullptr) {
    return nullptr;
  }

  // If the secret has been updated, update the proof source.
  if (context.get() != client_context_.get()) {
    client_context_ = context;
    crypto_config_ = std::make_shared<quic::QuicCryptoClientConfig>(
        std::make_unique<Quic::EnvoyQuicProofVerifier>(std::move(context)),
        quic_info_.getQuicSessionCacheDelegate());
  }
  // Return the latest client config.
  return crypto_config_;
}

std::unique_ptr<Network::ClientConnection>
Http3ConnPoolImpl::createClientConnection(Quic::QuicStatNames& quic_stat_names,
                                          Stats::Scope& scope) {
  auto source_address = host()->cluster().sourceAddress();
  if (!source_address.get()) {
    auto host_address = host()->address();
    source_address = Network::Utility::getLocalAddress(host_address->ip()->version());
  }
  return Quic::createQuicNetworkConnection(quic_info_, cryptoConfig(), server_id_, dispatcher(),
                                           host()->address(), source_address, quic_stat_names,
                                           scope);
}

std::unique_ptr<Http3ConnPoolImpl>
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state, Quic::QuicStatNames& quic_stat_names,
                 Stats::Scope& scope, OptRef<PoolConnectResultCallback> connect_callback,
                 Http::PersistentQuicInfo& quic_info) {
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
        data.connection_ = h3_pool->createClientConnection(quic_stat_names, scope);
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
        CodecClientPtr codec{new NoConnectCodecClientProd(
            CodecType::HTTP3, std::move(data.connection_), data.host_description_,
            pool->dispatcher(), pool->randomGenerator())};
        return codec;
      },
      std::vector<Protocol>{Protocol::Http3}, connect_callback, quic_info);
}

} // namespace Http3
} // namespace Http
} // namespace Envoy
