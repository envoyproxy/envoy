#include "contrib/reverse_connection/bootstrap/source/conn_pool.h"

#include <cstdint>

#include "source/common/http/http2/codec_impl.h"
#include "contrib/reverse_connection/bootstrap/source/reversed_connection_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ActiveClient::ActiveClient(Http::HttpConnPoolImplBase& parent,
                           OptRef<Upstream::Host::CreateConnectionData> data,
                           Http::CreateConnectionDataFn connection_fn)
    : Envoy::Http::Http2::ActiveClient(parent, data, connection_fn) {}

Http::ConnectionPool::InstancePtr
ReverseConnPoolFactoryImpl::allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Singleton::Manager& singleton_manager,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state,
                 absl::optional<Http::HttpServerPropertiesCache::Origin> origin,
                 Http::HttpServerPropertiesCacheSharedPtr cache) {
  ENVOY_LOG_MISC(debug, "Creating custom ActiveClient for reverse connections for host {}",
                 host->getHostId());
  return std::make_unique<Http::FixedHttpConnPoolImpl>(
      host, priority, dispatcher, options, transport_socket_options, random_generator, state,
      [&singleton_manager](Http::HttpConnPoolImplBase* pool) {
        return std::make_unique<ActiveClient>(
            *pool, absl::nullopt, [&singleton_manager](Http::HttpConnPoolImplBase& pool) {
                const Upstream::HostConstSharedPtr& host =
                    static_cast<Envoy::ConnectionPool::ConnPoolImplBase*>(&pool)->host();
                const std::string host_id = static_cast<std::string>(host->getHostId());
                const Upstream::ClusterInfo& cluster = host->cluster();
                if (cluster.type() != envoy::config::cluster::v3::Cluster_DiscoveryType::
                                        Cluster_DiscoveryType_REVERSE_CONNECTION) {
                ENVOY_LOG_MISC(debug, "not a reverse connection cluster");
                return host->createConnection(pool.dispatcher(), pool.socketOptions(),
                                              pool.transportSocketOptions());
                }
                ENVOY_LOG_MISC(trace, "reverse connection cluster: obtaining cached socket");

                // Retrieve the RevConnRegistry singleton and access the thread local slot
                std::shared_ptr<ReverseConnRegistry> reverse_conn_registry =
                        singleton_manager.getTyped<ReverseConnRegistry>("reverse_conn_registry_singleton");
                if (reverse_conn_registry == nullptr) {
                    throw EnvoyException(
                        "Cannot access cached reverse connection socket. Reverse connection registry not found");
                }
                Envoy::Extensions::Bootstrap::ReverseConnection::RCThreadLocalRegistry* thread_local_registry = reverse_conn_registry->getLocalRegistry();
                if (thread_local_registry == nullptr) {
                    throw EnvoyException("Cannot access cached reverse connection socket. Thread local reverse connection registry is null");
                }

                std::pair<Network::ConnectionSocketPtr, bool> host_socket_pair =
                    thread_local_registry->getRCHandler().getConnectionSocket(host_id, false);

                ENVOY_LOG_MISC(trace, "reverse connection cluster: obtained cached socket");
                Network::ConnectionSocketPtr&& host_socket = std::move(host_socket_pair.first);
                if (!host_socket) {
                    // fallback - try to create connection by connecting directly using address
                    ENVOY_LOG_MISC(
                        debug, "Could not find existing socket for host {}. Creating new connection",
                        host_id);
                    return host->createConnection(pool.dispatcher(), pool.socketOptions(),
                                                pool.transportSocketOptions());
                }
                const bool expects_proxy_protocol = host_socket_pair.second;
                ENVOY_LOG_MISC(debug,
                                "Found existing socket for host {}. Using reverse connection. "
                                "expects_proxy_protocol:{}",
                                host_id, expects_proxy_protocol);
                Network::TransportSocketPtr&& transport_socket =
                    host->transportSocketFactory().createTransportSocket(
                        pool.transportSocketOptions(), host);
                Network::Address::InstanceConstSharedPtr source_address =
                    cluster.getUpstreamLocalAddressSelector()
                        ->getUpstreamLocalAddress(host->address(), pool.socketOptions())
                        .address_;
                ENVOY_LOG_MISC(debug, "creating ReversedClientConnectionImpl over cached socket");
                Network::ClientConnectionPtr connection =
                    std::make_unique<ReversedClientConnectionImpl>(
                        host->address(), source_address, pool.dispatcher(),
                        std::move(transport_socket), std::move(host_socket),
                        *thread_local_registry, expects_proxy_protocol);
                connection->setBufferLimits(cluster.perConnectionBufferLimitBytes());
                cluster.createNetworkFilterChain(*connection);
                return Upstream::Host::CreateConnectionData{std::move(connection), std::move(host)};
                });
      },
      [](Upstream::Host::CreateConnectionData& data, Http::HttpConnPoolImplBase* pool) {
        Http::CodecClientPtr codec{new Http::CodecClientProd(
            Http::CodecType::HTTP2, std::move(data.connection_), data.host_description_,
            pool->dispatcher(), pool->randomGenerator(), pool->transportSocketOptions())};
        return codec;
      },
      std::vector<Http::Protocol>{Http::Protocol::Http2}, origin, cache);
}

REGISTER_FACTORY(ReverseConnPoolFactoryImpl, Http::Http2::ReverseConnPoolFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
