#pragma once

#include <cstdint>

#include "envoy/upstream/upstream.h"

#include "common/http/codec_client.h"
#include "common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {
namespace Http3 {

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

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state, TimeSource& time_source);

} // namespace Http3
} // namespace Http
} // namespace Envoy
