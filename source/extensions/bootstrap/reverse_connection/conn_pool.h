#pragma once

#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "source/common/http/http2/conn_pool.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Implementation of an active client for Reverse connections
 */
class ActiveClient : public Http::Http2::ActiveClient {
public:
  ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
               OptRef<Upstream::Host::CreateConnectionData> data,
               Http::CreateConnectionDataFn connection_fn = nullptr);
};

Http::ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state,
                 absl::optional<Http::HttpServerPropertiesCache::Origin> origin = absl::nullopt,
                 Http::HttpServerPropertiesCacheSharedPtr http_server_properties_cache = nullptr);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
