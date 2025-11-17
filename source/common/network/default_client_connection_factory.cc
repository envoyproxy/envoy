#include "source/common/network/default_client_connection_factory.h"

#include <memory>
#include <utility>

#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/connection_socket_impl.h"

#include "absl/status/statusor.h"

namespace Envoy {

namespace Network {

Network::ClientConnectionPtr DefaultClientConnectionFactory::createClientConnection(
    Event::Dispatcher& dispatcher, Network::Address::InstanceConstSharedPtr address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_options) {
  ASSERT(address->ip() || address->pipe());

  absl::StatusOr<std::unique_ptr<Network::ClientSocketImpl>> client_socket_or =
      ClientSocketImpl::create(address, options);
  RELEASE_ASSERT(client_socket_or.ok(),
                 absl::StrCat("failed to create socket: ", client_socket_or.status()));

  return std::make_unique<Network::ClientConnectionImpl>(
      dispatcher, std::move(*client_socket_or), source_address, std::move(transport_socket),
      options, transport_options);
}
REGISTER_FACTORY(DefaultClientConnectionFactory, Network::ClientConnectionFactory);

} // namespace Network
} // namespace Envoy
