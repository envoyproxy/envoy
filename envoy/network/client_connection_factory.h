#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/transport_socket.h"

namespace Envoy {
namespace Network {

// The factory to create a client connection. This factory hides the details of various remote
// address type and transport socket.
class ClientConnectionFactory : public Config::UntypedFactory {
public:
  ~ClientConnectionFactory() override = default;

  // Config::UntypedFactory
  std::string category() const override { return "network.connection.client"; }

  /**
   * @param address The target remote address.
   * @param source_address Optional source address.
   * @param transport_socket The transport socket which supports this client connection.
   * @param options The optional socket options.
   * @param transport socket options used to create the transport socket.
   * @return Network::ClientConnectionPtr The created connection. It's never nullptr but the return
   * connection may be closed upon return.
   */
  virtual Network::ClientConnectionPtr createClientConnection(
      Event::Dispatcher& dispatcher, Network::Address::InstanceConstSharedPtr address,
      Network::Address::InstanceConstSharedPtr source_address,
      Network::TransportSocketPtr&& transport_socket,
      const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_options) PURE;
};

} // namespace Network
} // namespace Envoy
