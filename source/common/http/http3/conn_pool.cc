#include "source/common/http/http3/conn_pool.h"

#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Http {
namespace Http3 {
namespace {

uint32_t getMaxStreams(const Upstream::ClusterInfo& cluster) {
  return PROTOBUF_GET_WRAPPED_OR_DEFAULT(cluster.http3Options().quic_protocol_options(),
                                         max_concurrent_streams, 100);
}

const Envoy::Ssl::ClientContextConfig&
getConfig(Network::UpstreamTransportSocketFactory& transport_socket_factory) {
  ASSERT(transport_socket_factory.clientContextConfig().has_value());
  return transport_socket_factory.clientContextConfig().value();
}

std::string sni(const Network::TransportSocketOptionsConstSharedPtr& options,
                Upstream::HostConstSharedPtr host) {
  return options && options->serverNameOverride().has_value()
             ? options->serverNameOverride().value()
             : getConfig(host->transportSocketFactory()).serverNameIndication();
}

} // namespace

ActiveClient::ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
                           Upstream::Host::CreateConnectionData& data)
    : MultiplexedActiveClientBase(
          parent, getMaxStreams(parent.host()->cluster()), getMaxStreams(parent.host()->cluster()),
          parent.host()->cluster().trafficStats().upstream_cx_http3_total_, data),
      async_connect_callback_(parent_.dispatcher().createSchedulableCallback([this]() {
        if (state() != Envoy::ConnectionPool::ActiveClient::State::Connecting) {
          return;
        }
        codec_client_->connect();
        if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http3_sends_early_data") &&
            readyForStream()) {
          // This client can send early data, so check if there are any pending streams can be sent
          // as early data.
          parent_.onUpstreamReadyForEarlyData(*this);
        }
      })) {
  ASSERT(codec_client_);
  if (dynamic_cast<CodecClientProd*>(codec_client_.get()) == nullptr) {
    ASSERT(Runtime::runtimeFeatureEnabled(
        "envoy.reloadable_features.postpone_h3_client_connect_to_next_loop"));
    // Hasn't called connect() yet, schedule one for the next event loop.
    async_connect_callback_->scheduleCallbackNextIteration();
  }
}

void ActiveClient::onMaxStreamsChanged(uint32_t num_streams) {
  updateCapacity(num_streams);
  if (state() == ActiveClient::State::Busy && currentUnusedCapacity() != 0) {
    ENVOY_BUG(hasHandshakeCompleted(), "Received MAX_STREAM frame before handshake completed.");
    parent_.transitionActiveClientState(*this, ActiveClient::State::Ready);
    // If there's waiting streams, make sure the pool will now serve them.
    parent_.onUpstreamReady();
  } else if (currentUnusedCapacity() == 0 && state() == ActiveClient::State::ReadyForEarlyData) {
    // With HTTP/3 this can only happen during a rejected 0-RTT handshake.
    parent_.transitionActiveClientState(*this, ActiveClient::State::Busy);
  }
}

ConnectionPool::Cancellable* Http3ConnPoolImpl::newStream(Http::ResponseDecoder& response_decoder,
                                                          ConnectionPool::Callbacks& callbacks,
                                                          const Instance::StreamOptions& options) {
  ENVOY_BUG(options.can_use_http3_,
            "Trying to send request over h3 while alternate protocols is disabled.");
  return FixedHttpConnPoolImpl::newStream(response_decoder, callbacks, options);
}

Http3ConnPoolImpl::Http3ConnPoolImpl(
    Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    Random::RandomGenerator& random_generator, Upstream::ClusterConnectivityState& state,
    CreateClientFn client_fn, CreateCodecFn codec_fn, std::vector<Http::Protocol> protocol,
    OptRef<PoolConnectResultCallback> connect_callback, Http::PersistentQuicInfo& quic_info)
    : FixedHttpConnPoolImpl(host, priority, dispatcher, options, transport_socket_options,
                            random_generator, state, client_fn, codec_fn, protocol, {}, nullptr),
      quic_info_(dynamic_cast<Quic::PersistentQuicInfoImpl&>(quic_info)),
      server_id_(sni(transport_socket_options, host),
                 static_cast<uint16_t>(host_->address()->ip()->port()), false),
      connect_callback_(connect_callback) {}

void Http3ConnPoolImpl::onConnected(Envoy::ConnectionPool::ActiveClient&) {
  if (connect_callback_ != absl::nullopt) {
    connect_callback_->onHandshakeComplete();
  }
}

void Http3ConnPoolImpl::onConnectFailed(Envoy::ConnectionPool::ActiveClient& client) {
  ASSERT(client.numActiveStreams() == 0);
  if (static_cast<ActiveClient&>(client).hasCreatedStream() && connect_callback_ != absl::nullopt) {
    connect_callback_->onZeroRttHandshakeFailed();
  }
}

// Make sure all connections are torn down before quic_info_ is deleted.
Http3ConnPoolImpl::~Http3ConnPoolImpl() { destructAllConnections(); }

std::unique_ptr<Network::ClientConnection>
Http3ConnPoolImpl::createClientConnection(Quic::QuicStatNames& quic_stat_names,
                                          OptRef<Http::HttpServerPropertiesCache> rtt_cache,
                                          Stats::Scope& scope) {
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config =
      host_->transportSocketFactory().getCryptoConfig();
  if (crypto_config == nullptr) {
    return nullptr; // no secrets available yet.
  }

  auto upstream_local_address_selector = host()->cluster().getUpstreamLocalAddressSelector();
  auto upstream_local_address =
      upstream_local_address_selector->getUpstreamLocalAddress(host()->address(), socketOptions());
  auto source_address = upstream_local_address.address_;

  if (source_address == nullptr) {
    auto host_address = host()->address();
    source_address = Network::Utility::getLocalAddress(host_address->ip()->version());
  }

  return Quic::createQuicNetworkConnection(
      quic_info_, std::move(crypto_config), server_id_, dispatcher(), host()->address(),
      source_address, quic_stat_names, rtt_cache, scope, upstream_local_address.socket_options_,
      transportSocketOptions(), connection_id_generator_);
}

std::unique_ptr<Http3ConnPoolImpl>
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state, Quic::QuicStatNames& quic_stat_names,
                 OptRef<Http::HttpServerPropertiesCache> rtt_cache, Stats::Scope& scope,
                 OptRef<PoolConnectResultCallback> connect_callback,
                 Http::PersistentQuicInfo& quic_info) {
  return std::make_unique<Http3ConnPoolImpl>(
      host, priority, dispatcher, options, transport_socket_options, random_generator, state,
      [&quic_stat_names, rtt_cache,
       &scope](HttpConnPoolImplBase* pool) -> ::Envoy::ConnectionPool::ActiveClientPtr {
        ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::pool), debug,
                            "Creating Http/3 client");
        // If there's no ssl context, the secrets are not loaded. Fast-fail by returning null.
        auto factory = &pool->host()->transportSocketFactory();
        if (factory->sslCtx() == nullptr) {
          ENVOY_LOG_EVERY_POW_2_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::pool),
                                          warn,
                                          "Failed to create Http/3 client. Transport socket "
                                          "factory is not configured correctly.");
          return nullptr;
        }
        Http3ConnPoolImpl* h3_pool = reinterpret_cast<Http3ConnPoolImpl*>(pool);
        Upstream::Host::CreateConnectionData data{};
        data.host_description_ = pool->host();
        data.connection_ = h3_pool->createClientConnection(quic_stat_names, rtt_cache, scope);
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
          // TODO(danzh) remove this branch once
          // "envoy.reloadable_features.postpone_h3_client_connect_to_next_loop" is deprecated.
          ASSERT(dynamic_cast<CodecClientProd*>(client->codec_client_.get()) != nullptr);
          return nullptr;
        }
        return client;
      },
      [](Upstream::Host::CreateConnectionData& data, HttpConnPoolImplBase* pool) {
        // Because HTTP/3 codec client connect() can close connection inline and can raise 0-RTT
        // event inline, and both cases need to have network callbacks and http callbacks wired up
        // to propagate the event, so do not call connect() during codec client construction.
        CodecClientPtr codec =
            Runtime::runtimeFeatureEnabled(
                "envoy.reloadable_features.postpone_h3_client_connect_to_next_loop")
                ? std::make_unique<NoConnectCodecClientProd>(
                      CodecType::HTTP3, std::move(data.connection_), data.host_description_,
                      pool->dispatcher(), pool->randomGenerator(), pool->transportSocketOptions())
                : std::make_unique<CodecClientProd>(
                      CodecType::HTTP3, std::move(data.connection_), data.host_description_,
                      pool->dispatcher(), pool->randomGenerator(), pool->transportSocketOptions());
        return codec;
      },
      std::vector<Protocol>{Protocol::Http3}, connect_callback, quic_info);
}

} // namespace Http3
} // namespace Http
} // namespace Envoy
