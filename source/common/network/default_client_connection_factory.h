#pragma once

#include "envoy/common/pure.h"
#include "envoy/network/client_connection_manager.h"
#include "envoy/network/connection.h"

namespace Envoy {

namespace Network {

class DefaultClientConnectionFactory : public Network::ClientConnectionFactory {
public:
  std::string name() override { return "default"; }
  Network::ClientConnectionPtr
  createClientConnection(Event::Dispatcher& dispatcher,
                         Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override;
};

} // namespace Network
} // namespace Envoy
