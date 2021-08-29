#include "source/common/http/http2/conn_pool.h"

#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "source/common/http/http2/codec_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {

namespace Http2 {
ActiveClient::ActiveClient(HttpConnPoolImplBase& parent)
    : MultiplexedActiveClientBase(
          parent, parent.host()->cluster().http2Options().max_concurrent_streams().value(),
          parent.host()->cluster().stats().upstream_cx_http2_total_) {}

ActiveClient::ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
                           Upstream::Host::CreateConnectionData& data)
    : MultiplexedActiveClientBase(
          parent, data, parent.host()->cluster().http2Options().max_concurrent_streams().value(),
          parent.host()->cluster().stats().upstream_cx_http2_total_) {}

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state) {
  return std::make_unique<FixedHttpConnPoolImpl>(
      host, priority, dispatcher, options, transport_socket_options, random_generator, state,
      [](HttpConnPoolImplBase* pool) { return std::make_unique<ActiveClient>(*pool); },
      [](Upstream::Host::CreateConnectionData& data, HttpConnPoolImplBase* pool) {
        CodecClientPtr codec{new CodecClientProd(CodecType::HTTP2, std::move(data.connection_),
                                                 data.host_description_, pool->dispatcher(),
                                                 pool->randomGenerator())};
        return codec;
      },
      std::vector<Protocol>{Protocol::Http2});
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
