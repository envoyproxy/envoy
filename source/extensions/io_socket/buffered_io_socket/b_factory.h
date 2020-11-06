#pragma once

#include "envoy/network/address.h"
#include "extensions/io_socket/config.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {

class BioClientConnectionFactory : public ClientConnectionFactory {
public:
  ~BioClientConnectionFactory() override = default;

  /**
   * Create a particular client connection.
   * @return ClientConnectionPtr the client connection.
   */
  Network::ClientConnectionPtr createClientConnection() override { return nullptr; }

  std::string name() const override { return std::string(Network::Address::EnvoyInternalName); }
};
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy