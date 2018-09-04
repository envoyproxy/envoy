#pragma once

#include "envoy/network/address.h"
#include "envoy/network/transport_socket.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/ssl/context_manager.h"

namespace Envoy {
namespace Ssl {

Network::TransportSocketFactoryPtr
createClientSslTransportSocketFactory(bool alpn, bool san, ContextManager& context_manager);
Network::TransportSocketFactoryPtr createUpstreamSslContext(ContextManager& context_manager);

Network::Address::InstanceConstSharedPtr getSslAddress(const Network::Address::IpVersion& version,
                                                       int port);

} // namespace Ssl
} // namespace Envoy
