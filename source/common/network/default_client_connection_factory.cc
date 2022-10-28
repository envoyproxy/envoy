#include "source/common/network/default_client_connection_factory.h"

#include "envoy/registry/registry.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"

namespace Envoy {

namespace Network {

Network::ClientConnectionPtr DefaultClientConnectionFactory::createClientConnection(
    Event::Dispatcher& dispatcher, Network::Address::InstanceConstSharedPtr address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_options) {
  ASSERT(address->ip() || address->pipe());
  return std::make_unique<Network::ClientConnectionImpl>(
      dispatcher, address, source_address, std::move(transport_socket), options, transport_options);
}
REGISTER_FACTORY(DefaultClientConnectionFactory, Network::ClientConnectionFactory);

} // namespace Network
} // namespace Envoy
