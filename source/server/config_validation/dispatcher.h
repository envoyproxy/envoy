#pragma once

#include "envoy/event/dispatcher.h"

#include "common/event/dispatcher_impl.h"

namespace Envoy {
namespace Event {

/**
 * Config-validation-only implementation of Event::Dispatcher. This class delegates all calls to
 * Event::DispatcherImpl, except for the methods involved with network events. Those methods are
 * disallowed at validation time, so they throw exceptions.
 */
class ValidationDispatcher : public DispatcherImpl {
public:
  Network::ClientConnectionPtr
      createClientConnection(Network::Address::InstanceConstSharedPtr) override;
  Network::ClientConnectionPtr
  createSslClientConnection(Ssl::ClientContext&, Network::Address::InstanceConstSharedPtr) override;
  Network::DnsResolverPtr createDnsResolver() override;
  Network::ListenerPtr createListener(Network::ConnectionHandler&, Network::ListenSocket&,
                                      Network::ListenerCallbacks&, Stats::Scope&,
                                      const Network::ListenerOptions&) override;
  Network::ListenerPtr createSslListener(Network::ConnectionHandler&, Ssl::ServerContext&,
                                         Network::ListenSocket&, Network::ListenerCallbacks&,
                                         Stats::Scope&, const Network::ListenerOptions&) override;
};

} // Event
} // Envoy
