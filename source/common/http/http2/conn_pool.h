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
  ActiveClient(HttpConnPoolImplBase& parent);
  ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
               Upstream::Host::CreateConnectionData& data);
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                 Upstream::ClusterConnectivityState& state);

} // namespace Http2
} // namespace Http
} // namespace Envoy
