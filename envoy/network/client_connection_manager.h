#pragma once

#include "envoy/network/connection.h"

namespace Envoy {
namespace Network {

class CCContext {};
class ClientConnectionFactory {
public:
  virtual ~ClientConnectionFactory() = default;
  std::string category() { return "network.connection"; }
  virtual std::string name() PURE;

  virtual Network::ClientConnectionPtr createClientConnection(const CCContext& context) PURE;
};

} // namespace Network
} // namespace Envoy
