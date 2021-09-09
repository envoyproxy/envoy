#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "envoy/network/connection.h"
#include "envoy/network/client_connection_manager.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace UserSpace {

class InternalClientConnectionFactory : public Network::ClientConnectionFactory {
public:
  std::string name() override { return "EnvoyInternal"; }
  Network::ClientConnectionPtr createClientConnection(const Network::CCContext&) override;
};

} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
