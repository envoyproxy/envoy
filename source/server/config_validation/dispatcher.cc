#include "server/config_validation/dispatcher.h"

#include "common/common/assert.h"

#include "server/config_validation/connection.h"

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
    const std::vector<Network::Address::InstanceConstSharedPtr>&) {
  NOT_IMPLEMENTED;
}

Network::ListenerPtr ValidationDispatcher::createListener(Network::Socket&,
                                                          Network::ListenerCallbacks&, bool, bool) {
  NOT_IMPLEMENTED;
}

} // namespace Event
} // namespace Envoy
