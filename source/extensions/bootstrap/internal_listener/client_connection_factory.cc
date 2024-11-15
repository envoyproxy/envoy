#include "source/extensions/bootstrap/internal_listener/client_connection_factory.h"

#include "envoy/registry/registry.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/extensions/io_socket/user_space/io_handle_impl.h"

namespace Envoy {

namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

ThreadLocal::TypedSlot<Bootstrap::InternalListener::ThreadLocalRegistryImpl>*
    InternalClientConnectionFactory::registry_tls_slot_ = nullptr;
// The default buffer size is 1024 KiB.
uint32_t InternalClientConnectionFactory::buffer_size_ =
    InternalClientConnectionFactory::DefaultBufferSize;

Network::ClientConnectionPtr InternalClientConnectionFactory::createClientConnection(
    Event::Dispatcher& dispatcher, Network::Address::InstanceConstSharedPtr address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_options) {
  // OS does not fill the address automatically so a pivotal address is populated.
  source_address =
      std::make_shared<Network::Address::EnvoyInternalInstance>("internal_client_address");

  ENVOY_LOG(debug, "Internal client connection buffer size {}.", buffer_size_);
  auto [io_handle_client, io_handle_server] =
      Extensions::IoSocket::UserSpace::IoHandleFactory::createBufferLimitedIoHandlePair(
          buffer_size_);

  auto client_conn = std::make_unique<Network::ClientConnectionImpl>(
      dispatcher,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_client), source_address,
                                                      address),
      source_address, std::move(transport_socket), options, transport_options);

  if (registry_tls_slot_ == nullptr || !registry_tls_slot_->get().has_value()) {
    ENVOY_LOG_MISC(debug,
                   "server has not initialized internal listener registry, close the connection");
    io_handle_server->close();
    return client_conn;
  }

  // It's either in the main thread or the worker is not yet started.
  auto internal_listener_manager = registry_tls_slot_->get()->getInternalListenerManager();
  if (!internal_listener_manager.has_value()) {
    io_handle_server->close();
    return client_conn;
  }

  // The requested internal listener is not available yet.
  auto internal_listener = internal_listener_manager.value().get().findByAddress(address);
  if (!internal_listener.has_value()) {
    io_handle_server->close();
    return client_conn;
  }

  auto accepted_socket = std::make_unique<Network::AcceptedSocketImpl>(
      std::move(io_handle_server), address, source_address, absl::nullopt, false);
  internal_listener->onAccept(std::move(accepted_socket));
  return client_conn;
}

REGISTER_FACTORY(InternalClientConnectionFactory, Network::ClientConnectionFactory);

} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
