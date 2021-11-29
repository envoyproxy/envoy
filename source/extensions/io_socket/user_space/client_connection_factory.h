#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/network/client_connection_factory.h"
#include "envoy/network/connection.h"

#include "source/common/common/logger.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace UserSpace {

class InternalClientConnectionFactory : public Network::ClientConnectionFactory,
                                        Logger::Loggable<Logger::Id::connection> {
public:
  ~InternalClientConnectionFactory() override = default;
  std::string name() const override { return "EnvoyInternal"; }
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
