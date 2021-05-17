#pragma once

#include <cstdint>

#include "envoy/upstream/upstream.h"

#include "common/http/codec_client.h"
#include "common/http/conn_pool_base.h"

#ifdef ENVOY_ENABLE_QUIC
#include "common/quic/client_connection_factory_impl.h"
#include "common/quic/envoy_quic_utils.h"
#else
#error "http3 conn pool should not be built with QUIC disabled"
#endif

namespace Envoy {
namespace Http {
namespace Http3 {
class Http3ConnPoolImplTest;

void setQuicConfigFromClusterConfig(const Upstream::ClusterInfo& cluster,
                                    quic::QuicConfig& quic_config);

class ActiveClient : public MultiplexedActiveClientBase {
public:
  ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
               Upstream::Host::CreateConnectionData& data)
      : MultiplexedActiveClientBase(parent,
                                    parent.host()
                                        ->cluster()
                                        .http3Options()
                                        .quic_protocol_options()
                                        .max_concurrent_streams()
                                        .value(),
                                    parent.host()->cluster().stats().upstream_cx_http3_total_,
                                    data) {}
};

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
        dispatcher, transport_socket_factory, time_source, source_address);
    setQuicConfigFromClusterConfig(host_->cluster(), quic_info_->quic_config_);
  }

  Quic::PersistentQuicInfoImpl& quicInfo() { return *quic_info_; }

  // Make sure all connections are torn down before quic_info_ is deleted.
  ~Http3ConnPoolImpl() override { destructAllConnections(); }

private:
  // Store quic helpers which can be shared between connections and must live
  // beyond the lifetime of individual connections.
  std::unique_ptr<Quic::PersistentQuicInfoImpl> quic_info_;
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state, TimeSource& time_source);

} // namespace Http3
} // namespace Http
} // namespace Envoy
