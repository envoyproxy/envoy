#include "source/common/http/http2/reverse_conn_pool_factory.h"

#include "source/common/http/http2/conn_pool.h"

namespace Envoy {
namespace Http {
namespace Http2 {

ConnectionPool::InstancePtr DefaultReverseConnPoolFactory::allocateConnPool(
    Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator, Singleton::Manager&,
    Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    Upstream::ClusterConnectivityState& state,
    absl::optional<HttpServerPropertiesCache::Origin> origin,
    Http::HttpServerPropertiesCacheSharedPtr http_server_properties_cache) {
  // The singleton_manager parameter is currently unused but kept for future extensibility
  return Http2::allocateConnPool(dispatcher, random_generator, host, priority, options,
                                 transport_socket_options, state, origin,
                                 http_server_properties_cache);
}

REGISTER_FACTORY(DefaultReverseConnPoolFactory, ReverseConnPoolFactory);

} // namespace Http2
} // namespace Http
} // namespace Envoy
