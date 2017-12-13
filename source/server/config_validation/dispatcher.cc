#include "server/config_validation/dispatcher.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Event {

Network::ClientConnectionPtr
ValidationDispatcher::createClientConnection(Network::Address::InstanceConstSharedPtr,
                                             Network::Address::InstanceConstSharedPtr,
                                             Network::TransportSocketPtr&&) {
  NOT_IMPLEMENTED;
}

Network::DnsResolverSharedPtr ValidationDispatcher::createDnsResolver(
    const std::vector<Network::Address::InstanceConstSharedPtr>&) {
  NOT_IMPLEMENTED;
}

Network::ListenerPtr ValidationDispatcher::createListener(Network::ConnectionHandler&,
                                                          Network::ListenSocket&,
                                                          Network::ListenerCallbacks&,
                                                          Stats::Scope&,
                                                          const Network::ListenerOptions&) {
  NOT_IMPLEMENTED;
}

Network::ListenerPtr
ValidationDispatcher::createSslListener(Network::ConnectionHandler&, Ssl::ServerContext&,
                                        Network::ListenSocket&, Network::ListenerCallbacks&,
                                        Stats::Scope&, const Network::ListenerOptions&) {
  NOT_IMPLEMENTED;
}

} // namespace Event
} // namespace Envoy
