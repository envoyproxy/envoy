#pragma once

#include "envoy/event/dispatcher.h"

#include "source/common/event/dispatcher_impl.h"

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
  ValidationDispatcher(const std::string& name, Api::Api& api, Event::TimeSystem& time_system)
      : DispatcherImpl(name, api, time_system) {}

  Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr,
                         Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&&,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override;
  Network::DnsResolverSharedPtr createDnsResolver(
      const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
      const envoy::config::core::v3::DnsResolverOptions& dns_resolver_options) override;
  Network::ListenerPtr createListener(Network::SocketSharedPtr&&, Network::TcpListenerCallbacks&,
                                      bool bind_to_port) override;

protected:
  std::shared_ptr<Network::ValidationDnsResolver> dns_resolver_{
      std::make_shared<Network::ValidationDnsResolver>()};
};

} // namespace Event
} // namespace Envoy
