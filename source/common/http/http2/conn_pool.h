#pragma once

#include <cstdint>

#include "envoy/upstream/upstream.h"

#include "source/common/http/codec_client.h"
#include "source/common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {

namespace Http2 {

/**
 * Implementation of an active client for HTTP/2
 */
class ActiveClient : public MultiplexedActiveClientBase {
public:
  // Calculate the expected streams allowed for this host, based on both
  // configuration and cached SETTINGS.
  static uint32_t calculateInitialStreamsLimit(
      Http::HttpServerPropertiesCacheSharedPtr http_server_properties_cache,
      absl::optional<HttpServerPropertiesCache::Origin>& origin,
      Upstream::HostDescriptionConstSharedPtr host);

  ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
               OptRef<Upstream::Host::CreateConnectionData> data,
               CreateConnectionDataFn connection_fn = nullptr);
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state,
                 absl::optional<HttpServerPropertiesCache::Origin> origin = absl::nullopt,
                 Http::HttpServerPropertiesCacheSharedPtr http_server_properties_cache = nullptr);

/**
 * Abstract class for allocating reverse connection pools.
 */
class ReverseConnPoolFactory : public Config::UntypedFactory {
public:
  virtual ~ReverseConnPoolFactory() = default;

  virtual ConnectionPool::InstancePtr allocateConnPool(
      Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
      Singleton::Manager& singleton_manager, Upstream::HostConstSharedPtr host,
      Upstream::ResourcePriority priority, const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
      Upstream::ClusterConnectivityState& state,
      absl::optional<HttpServerPropertiesCache::Origin> origin = absl::nullopt,
      Http::HttpServerPropertiesCacheSharedPtr http_server_properties_cache = nullptr) PURE;

  std::string category() const override { return "envoy.http.reverse_conn"; }
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
