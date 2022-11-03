#include "source/common/network/resolver_impl.h"

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "source/common/config/well_known_names.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Implementation of a resolver for IP addresses.
 */
class IpResolver : public Resolver {

public:
  InstanceConstSharedPtr
  resolve(const envoy::config::core::v3::SocketAddress& socket_address) override {
    switch (socket_address.port_specifier_case()) {
    case envoy::config::core::v3::SocketAddress::PortSpecifierCase::kPortValue:
    // Default to port 0 if no port value is specified.
    case envoy::config::core::v3::SocketAddress::PortSpecifierCase::PORT_SPECIFIER_NOT_SET:
      return Network::Utility::parseInternetAddress(
          socket_address.address(), socket_address.port_value(), !socket_address.ipv4_compat());
    case envoy::config::core::v3::SocketAddress::PortSpecifierCase::kNamedPort:
      break;
    }
    throw EnvoyException(fmt::format("IP resolver can't handle port specifier type {}",
                                     socket_address.port_specifier_case()));
  }

  std::string name() const override { return Config::AddressResolverNames::get().IP; }
};

/**
 * Static registration for the IP resolver. @see RegisterFactory.
 */
REGISTER_FACTORY(IpResolver, Resolver);

InstanceConstSharedPtr resolveProtoAddress(const envoy::config::core::v3::Address& address) {
  switch (address.address_case()) {
  case envoy::config::core::v3::Address::AddressCase::ADDRESS_NOT_SET:
    throw EnvoyException("Address must be set: " + address.DebugString());
  case envoy::config::core::v3::Address::AddressCase::kSocketAddress:
    return resolveProtoSocketAddress(address.socket_address());
  case envoy::config::core::v3::Address::AddressCase::kPipe:
    return std::make_shared<PipeInstance>(address.pipe().path(), address.pipe().mode());
  case envoy::config::core::v3::Address::AddressCase::kEnvoyInternalAddress:
    switch (address.envoy_internal_address().address_name_specifier_case()) {
    case envoy::config::core::v3::EnvoyInternalAddress::AddressNameSpecifierCase::
        kServerListenerName:
      return std::make_shared<EnvoyInternalInstance>(
          address.envoy_internal_address().server_listener_name(),
          address.envoy_internal_address().endpoint_id());
    case envoy::config::core::v3::EnvoyInternalAddress::AddressNameSpecifierCase::
        ADDRESS_NAME_SPECIFIER_NOT_SET:
      break;
    }
  }
  throw EnvoyException("Failed to resolve address:" + address.DebugString());
}

InstanceConstSharedPtr
resolveProtoSocketAddress(const envoy::config::core::v3::SocketAddress& socket_address) {
  Resolver* resolver = nullptr;
  const std::string& resolver_name = socket_address.resolver_name();
  if (resolver_name.empty()) {
    resolver =
        Registry::FactoryRegistry<Resolver>::getFactory(Config::AddressResolverNames::get().IP);
  } else {
    resolver = Registry::FactoryRegistry<Resolver>::getFactory(resolver_name);
  }
  if (resolver == nullptr) {
    throw EnvoyException(fmt::format("Unknown address resolver: {}", resolver_name));
  }
  return resolver->resolve(socket_address);
}

} // namespace Address
} // namespace Network
} // namespace Envoy
