#pragma once

#include "envoy/common/pure.h"
#include "envoy/network/client_connection_factory.h"
#include "envoy/network/connection.h"

namespace Envoy {

namespace Network {

class DefaultClientConnectionFactory : public Network::ClientConnectionFactory {
public:
  ~DefaultClientConnectionFactory() override = default;
  std::string name() const override { return "default"; }
  Network::ClientConnectionPtr
  createClientConnection(Event::Dispatcher& dispatcher,
                         Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override;
};

} // namespace Network
} // namespace Envoy
