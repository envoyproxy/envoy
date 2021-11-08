#include "source/extensions/io_socket/user_space/client_connection_factory.h"

#include "envoy/registry/registry.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/extensions/io_socket/user_space/io_handle_impl.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace UserSpace {

Network::ClientConnectionPtr InternalClientConnectionFactory::createClientConnection(
    Event::Dispatcher& dispatcher, Network::Address::InstanceConstSharedPtr address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options) {

  auto [io_handle_client, io_handle_server] =
      Extensions::IoSocket::UserSpace::IoHandleFactory::createIoHandlePair();

  auto client_conn = std::make_unique<Network::ClientConnectionImpl>(
      dispatcher,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_client), source_address,
                                                      address),
      source_address, std::move(transport_socket), options);

  // It's either in the main thread or the worker is not yet started.
  auto internal_listener_manager = dispatcher.getInternalListenerManager();
  if (!internal_listener_manager.has_value()) {
    io_handle_server->close();
    return client_conn;
  }
  
  // The request internal listener may not exist.
  auto internal_listener = internal_listener_manager.value().get().findByAddress(address);
  if (!internal_listener.has_value()) {
    io_handle_server->close();
    return client_conn;
  }

  auto accepted_socket = std::make_unique<Network::AcceptedSocketImpl>(std::move(io_handle_server),
                                                                       address, source_address);
  internal_listener->get().onAccept(std::move(accepted_socket));
  return client_conn;
}

REGISTER_FACTORY(InternalClientConnectionFactory, Network::ClientConnectionFactory);

} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
