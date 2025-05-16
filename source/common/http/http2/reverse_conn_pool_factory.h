#pragma once

#include "source/common/http/http2/conn_pool.h"

namespace Envoy {
namespace Http {
namespace Http2 {

class DefaultReverseConnPoolFactory : public ReverseConnPoolFactory {
public:
  ConnectionPool::InstancePtr allocateConnPool(
      Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
      Singleton::Manager& singleton_manager, Upstream::HostConstSharedPtr host,
      Upstream::ResourcePriority priority,
      const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
      Upstream::ClusterConnectivityState& state,
      absl::optional<HttpServerPropertiesCache::Origin> origin = absl::nullopt,
      Http::HttpServerPropertiesCacheSharedPtr http_server_properties_cache = nullptr) override;

  std::string name() const override { return "envoy.upstreams.http.reverse_conn.default"; }
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
