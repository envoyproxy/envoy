#include "server/config_validation/dispatcher.h"

namespace Event {

Network::ClientConnectionPtr
    ValidationDispatcher::createClientConnection(Network::Address::InstanceConstSharedPtr) {
  throw EnvoyException("ValidationDispatcher::createClientConnection() not implemented.");
}

Network::ClientConnectionPtr
ValidationDispatcher::createSslClientConnection(Ssl::ClientContext&,
                                                Network::Address::InstanceConstSharedPtr) {
  throw EnvoyException("ValidationDispatcher::createSslClientConnection() not implemented.");
}

Network::DnsResolverPtr ValidationDispatcher::createDnsResolver() {
  throw EnvoyException("ValidationDispatcher::createDnsResolver() not implemented.");
}

Network::ListenerPtr ValidationDispatcher::createListener(Network::ConnectionHandler&,
                                                          Network::ListenSocket&,
                                                          Network::ListenerCallbacks&,
                                                          Stats::Scope&,
                                                          const Network::ListenerOptions&) {
  throw EnvoyException("ValidationDispatcher::createListener() not implemented.");
}

Network::ListenerPtr
ValidationDispatcher::createSslListener(Network::ConnectionHandler&, Ssl::ServerContext&,
                                        Network::ListenSocket&, Network::ListenerCallbacks&,
                                        Stats::Scope&, const Network::ListenerOptions&) {
  throw EnvoyException("ValidationDispatcher::CreateSslListener() not implemented.");
}

} // Event
