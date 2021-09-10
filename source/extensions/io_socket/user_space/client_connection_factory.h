#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/network/client_connection_manager.h"
#include "envoy/network/connection.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace UserSpace {

class InternalClientConnectionFactory : public Network::ClientConnectionFactory {
public:
  std::string name() override { return "EnvoyInternal"; }
  Network::ClientConnectionPtr
  createClientConnection(Event::Dispatcher& dispatcher,
                         Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override;
};

} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
