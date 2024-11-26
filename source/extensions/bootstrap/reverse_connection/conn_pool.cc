#include "source/extensions/bootstrap/reverse_connection/conn_pool.h"

#include <cstdint>

#include "source/common/http/http2/codec_impl.h"
#include "source/common/network/reversed_connection_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ActiveClient::ActiveClient(Http::HttpConnPoolImplBase& parent,
                           OptRef<Upstream::Host::CreateConnectionData> data,
                           Http::CreateConnectionDataFn connection_fn)
    : Envoy::Http::Http2::ActiveClient(parent, data, connection_fn) {}

Http::ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
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
      [](Http::HttpConnPoolImplBase* pool) {
        return std::make_unique<ActiveClient>(
            *pool, absl::nullopt, [](Http::HttpConnPoolImplBase& pool) {
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

              std::pair<Network::ConnectionSocketPtr, bool> host_socket_pair =
                  pool.dispatcher()
                      .connectionHandler()
                      ->reverseConnRegistry()
                      .getRCHandler()
                      .getConnectionSocket(host_id, false);
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
                  std::make_unique<Network::ReversedClientConnectionImpl>(
                      host->address(), source_address, pool.dispatcher(),
                      std::move(transport_socket), std::move(host_socket), expects_proxy_protocol);
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

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
