#include "source/extensions/io_socket/user_space/client_connection_factory.h"

#include "envoy/registry/registry.h"

#include "source/extensions/io_socket/user_space/io_handle_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/listen_socket_impl.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace UserSpace {

Network::ClientConnectionPtr
InternalClientConnectionFactory::createClientConnection(const Network::CCContext&) {
  // TODO: fetch from arg

  Network::Address::InstanceConstSharedPtr address;
  Network::Address::InstanceConstSharedPtr source_address;
  Network::TransportSocketPtr transport_socket;
  const Network::ConnectionSocket::OptionsSharedPtr options;
  Event::Dispatcher& dispatcher = *reinterpret_cast<Event::Dispatcher*>(1);

  Network::IoHandlePtr io_handle_client;
  Network::IoHandlePtr io_handle_server;

  std::tie(io_handle_client, io_handle_server) =
      Extensions::IoSocket::UserSpace::IoHandleFactory::createIoHandlePair();

  auto client_conn = std::make_unique<Network::ClientConnectionImpl>(
      *this, std::move(io_handle_client), address, source_address, std::move(transport_socket),
      options);
  // TODO(lambdai): refactor nested if.
  auto internal_listener_manager = dispatcher.getInternalListenerManager();
  if (internal_listener_manager.has_value()) {
    // It's either in main thread or the worker is not yet started.
    auto internal_listener = internal_listener_manager.value().get().findByAddress(address);
    if (internal_listener.has_value()) {
      auto original_address = address;
      // if (options != nullptr) {
      //   for (const auto& opt : *options) {
      //     auto* internal_opt = dynamic_cast<const Network::InternalSocketOptionImpl*>(opt.get());
      //     if (internal_opt != nullptr) {
      //       original_address = internal_opt->original_remote_address_;
      //     }
      //   }
      // }
      auto accepted_socket = std::make_unique<Network::AcceptedSocketImpl>(
          std::move(io_handle_server), original_address, source_address);
      // TODO: also check if disabled
      internal_listener.value().get().onAccept(std::move(accepted_socket));
      FANCY_LOG(debug, "lambdai: find internal listener {} ", address->asStringView());
    } else {
      FANCY_LOG(debug, "lambdai: cannot find internal listener {} ", address->asStringView());
      // injected error into client_conn;
      io_handle_server->close();
    }
    return client_conn;
  } else {
    // TODO(lambdai): return closed connection.
    return nullptr;
  }
}
REGISTER_FACTORY(InternalClientConnectionFactory, Network::ClientConnectionFactory);

} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
} // namespace Envoy
