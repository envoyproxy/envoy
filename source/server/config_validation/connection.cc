#include "server/config_validation/connection.h"

namespace Envoy {
namespace Network {

std::unique_ptr<ConfigValidateConnection>
ConfigValidateConnection::create(Event::ValidationDispatcher& dispatcher,
                                 const Address::InstanceConstSharedPtr& remote_address,
                                 const Address::InstanceConstSharedPtr& source_address,
                                 Network::TransportSocketPtr&& transport_socket,
                                 const Network::ConnectionSocket::OptionsSharedPtr& options) {
  // make_shared<> requires a public constructor so we can't use it here.
  std::unique_ptr<ConfigValidateConnection> conn(new ConfigValidateConnection(
      dispatcher, remote_address, source_address, std::move(transport_socket), options));
  conn->transport_socket_->setTransportSocketCallbacks(*conn);
  return conn;
}

} // namespace Network
} // namespace Envoy
