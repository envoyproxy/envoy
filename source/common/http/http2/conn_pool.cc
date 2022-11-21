#include "source/common/http/http2/conn_pool.h"

#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "source/common/http/http2/codec_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {

namespace Http2 {

uint32_t ActiveClient::calculateInitialStreamsLimit(
    Http::HttpServerPropertiesCacheSharedPtr http_server_properties_cache,
    absl::optional<HttpServerPropertiesCache::Origin>& origin,
    Upstream::HostDescriptionConstSharedPtr host) {
  uint32_t initial_streams = host->cluster().http2Options().max_concurrent_streams().value();
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.allow_concurrency_for_alpn_pool")) {
    return initial_streams;
  }
  if (http_server_properties_cache && origin.has_value()) {
    uint32_t cached_concurrency =
        http_server_properties_cache->getConcurrentStreams(origin.value());
    if (cached_concurrency != 0 && cached_concurrency < initial_streams) {
      // Only use the cached concurrency if lowers the streams below the
      // configured max_concurrent_streams as Envoy should never send more
      // than max_concurrent_streams at once.
      initial_streams = cached_concurrency;
    }
  }
  uint64_t max_requests = MultiplexedActiveClientBase::maxStreamsPerConnection(
      host->cluster().maxRequestsPerConnection());
  if (max_requests < initial_streams) {
    initial_streams = max_requests;
  }
  return initial_streams;
}

ActiveClient::ActiveClient(HttpConnPoolImplBase& parent,
                           OptRef<Upstream::Host::CreateConnectionData> data)
    : MultiplexedActiveClientBase(
          parent, calculateInitialStreamsLimit(parent.cache(), parent.origin(), parent.host()),
          parent.host()->cluster().http2Options().max_concurrent_streams().value(),
          parent.host()->cluster().trafficStats().upstream_cx_http2_total_, data) {}

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state,
                 absl::optional<HttpServerPropertiesCache::Origin> origin,
                 Http::HttpServerPropertiesCacheSharedPtr cache) {
  return std::make_unique<FixedHttpConnPoolImpl>(
      host, priority, dispatcher, options, transport_socket_options, random_generator, state,
      [](HttpConnPoolImplBase* pool) {
        return std::make_unique<ActiveClient>(*pool, absl::nullopt);
      },
      [](Upstream::Host::CreateConnectionData& data, HttpConnPoolImplBase* pool) {
        CodecClientPtr codec{new CodecClientProd(
            CodecType::HTTP2, std::move(data.connection_), data.host_description_,
            pool->dispatcher(), pool->randomGenerator(), pool->transportSocketOptions())};
        return codec;
      },
      std::vector<Protocol>{Protocol::Http2}, origin, cache);
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
