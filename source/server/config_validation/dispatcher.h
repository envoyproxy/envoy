#pragma once

#include "envoy/event/dispatcher.h"

#include "common/event/dispatcher_impl.h"

#include "dns.h"

namespace Envoy {
namespace Event {

/**
 * Config-validation-only implementation of Event::Dispatcher. This class delegates all calls to
 * Event::DispatcherImpl, except for the methods involved with network events. Those methods are
 * disallowed at validation time.
 */
class ValidationDispatcher : public DispatcherImpl {
public:
  ValidationDispatcher(TimeSource& time_source) : DispatcherImpl(time_source) {}

  Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr,
                         Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&&,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override;
  Network::DnsResolverSharedPtr createDnsResolver(
      const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers) override;
  Network::ListenerPtr createListener(Network::Socket&, Network::ListenerCallbacks&,
                                      bool bind_to_port,
                                      bool hand_off_restored_destination_connections) override;

protected:
  std::shared_ptr<Network::ValidationDnsResolver> dns_resolver_{
      std::make_shared<Network::ValidationDnsResolver>()};
};

} // namespace Event
} // namespace Envoy
