#include "server/config_validation/dispatcher.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Event {

Network::ClientConnectionPtr ValidationDispatcher::createClientConnection(
    Network::Address::InstanceConstSharedPtr, Network::Address::InstanceConstSharedPtr,
    Network::TransportSocketPtr&&, const Network::ConnectionSocket::OptionsSharedPtr&) {
  NOT_IMPLEMENTED;
}

Network::DnsResolverSharedPtr ValidationDispatcher::createDnsResolver(
    const std::vector<Network::Address::InstanceConstSharedPtr>&) {
  NOT_IMPLEMENTED;
}

Network::ListenerPtr ValidationDispatcher::createListener(Network::ListenSocket&,
                                                          Network::ListenerCallbacks&, bool, bool) {
  NOT_IMPLEMENTED;
}

} // namespace Event
} // namespace Envoy
