#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"
#include "envoy/network/connection.h"

#include "extensions/io_socket/address_map.h"

#include "common/common/assert.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {

class ClientConnectionFactory : public Envoy::Config::UntypedFactory {
public:
  ~ClientConnectionFactory() override = default;

  /**
   * Create a particular client connection.
   * @return ClientConnectionPtr the client connection.
   */
  virtual Network::ClientConnectionPtr createClientConnection() PURE;

  std::string category() const override { return "envoy.connection"; }

  /**
   * Convenience method to lookup a factory by destination address type.
   * @return ClientConnectionFactory& for the destiantion address.
   */
  static ClientConnectionFactory*
  getFactoryByAddress(Network::Address::InstanceConstSharedPtr& destination_address) {
    if (destination_address == nullptr) {
      return nullptr;
    }

    return Registry::FactoryRegistry<ClientConnectionFactory>::getFactory(
        Network::Address::addressMap[destination_address->type()]);
  }
};

} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy