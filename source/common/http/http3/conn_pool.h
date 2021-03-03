#pragma once

#include <cstdint>

#include "envoy/upstream/upstream.h"

#include "common/http/codec_client.h"
#include "common/http/http2/conn_pool.h"

namespace Envoy {
namespace Http {
namespace Http3 {

// TODO(#14829) the constructor of Http2::ActiveClient sets max requests per
// connection based on HTTP/2 config. Sort out the HTTP/3 config story.
class ActiveClient : public MultiplexedActiveClientBase {
public:
  ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
               Upstream::Host::CreateConnectionData& data)
      : MultiplexedActiveClientBase(
            parent, parent.host()->cluster().stats().upstream_cx_http3_total_, data) {}
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
