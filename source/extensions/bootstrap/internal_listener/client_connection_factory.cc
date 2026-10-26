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

namespace {

/**
 * Client connection to an internal listener. If the connection has no failure reason of its own,
 * it reports the close details that the internal listener side recorded in the shared
 * PassthroughState, e.g. that tcp_proxy closed the connection because it failed to connect to its
 * upstream. This lets the client side tell such a failure apart from a bare connection termination.
 */
class InternalClientConnectionImpl : public Network::ClientConnectionImpl {
public:
  InternalClientConnectionImpl(
      Event::Dispatcher& dispatcher, std::unique_ptr<Network::ConnectionSocket> socket,
      const Network::Address::InstanceConstSharedPtr& source_address,
      Network::TransportSocketPtr&& transport_socket,
      const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_options,
      IoSocket::UserSpace::PassthroughStateSharedPtr passthrough_state)
      : Network::ClientConnectionImpl(dispatcher, std::move(socket), source_address,
                                      std::move(transport_socket), options, transport_options),
        passthrough_state_(std::move(passthrough_state)) {}

  // Network::Connection
  absl::string_view transportFailureReason() const override {
    absl::string_view reason = Network::ClientConnectionImpl::transportFailureReason();
    if (reason.empty() && passthrough_state_ != nullptr) {
      return passthrough_state_->closeDetails();
    }
    return reason;
  }

private:
  const IoSocket::UserSpace::PassthroughStateSharedPtr passthrough_state_;
};

} // namespace

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

  IoSocket::UserSpace::PassthroughStateSharedPtr passthrough_state =
      io_handle_client->passthroughState();
  auto client_conn = std::make_unique<InternalClientConnectionImpl>(
      dispatcher,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_client), source_address,
                                                      address),
      source_address, std::move(transport_socket), options, transport_options,
      std::move(passthrough_state));

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
      std::move(io_handle_server), address, source_address, std::nullopt, false);
  internal_listener->onAccept(std::move(accepted_socket));
  return client_conn;
}

REGISTER_FACTORY(InternalClientConnectionFactory, Network::ClientConnectionFactory);

} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
