#include "source/server/config_validation/dispatcher.h"

#include "source/common/common/assert.h"
#include "source/server/config_validation/connection.h"

namespace Envoy {
namespace Event {

Network::ClientConnectionPtr ValidationDispatcher::createClientConnection(
    Network::Address::InstanceConstSharedPtr remote_address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options) {
  return std::make_unique<Network::ConfigValidateConnection>(*this, remote_address, source_address,
                                                             std::move(transport_socket), options);
}

Network::DnsResolverSharedPtr ValidationDispatcher::createDnsResolver(
    const std::vector<Network::Address::InstanceConstSharedPtr>&,
    const envoy::config::core::v3::DnsResolverOptions&) {
  return dns_resolver_;
}

Network::ListenerPtr ValidationDispatcher::createListener(Network::SocketSharedPtr&&,
                                                          Network::TcpListenerCallbacks&, bool) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

} // namespace Event
} // namespace Envoy
