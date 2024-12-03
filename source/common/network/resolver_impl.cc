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
  absl::StatusOr<InstanceConstSharedPtr>
  resolve(const envoy::config::core::v3::SocketAddress& socket_address) override {
    switch (socket_address.port_specifier_case()) {
    case envoy::config::core::v3::SocketAddress::PortSpecifierCase::kPortValue:
    // Default to port 0 if no port value is specified.
    case envoy::config::core::v3::SocketAddress::PortSpecifierCase::PORT_SPECIFIER_NOT_SET: {
      auto addr = Network::Utility::parseInternetAddressNoThrow(
          socket_address.address(), socket_address.port_value(), !socket_address.ipv4_compat());
      if (!addr) {
        return absl::InvalidArgumentError(
            absl::StrCat("malformed IP address: ", socket_address.address()));
      }
      return addr;
    }
    case envoy::config::core::v3::SocketAddress::PortSpecifierCase::kNamedPort:
      break;
    }
    return absl::InvalidArgumentError(
        fmt::format("IP resolver can't handle port specifier type {}",
                    static_cast<int>(socket_address.port_specifier_case())));
  }

  std::string name() const override { return Config::AddressResolverNames::get().IP; }
};

/**
 * Static registration for the IP resolver. @see RegisterFactory.
 */
REGISTER_FACTORY(IpResolver, Resolver);

absl::StatusOr<InstanceConstSharedPtr>
resolveProtoAddress(const envoy::config::core::v3::Address& address) {
  switch (address.address_case()) {
  case envoy::config::core::v3::Address::AddressCase::ADDRESS_NOT_SET:
    return absl::InvalidArgumentError("Address must be set: " + address.DebugString());
  case envoy::config::core::v3::Address::AddressCase::kSocketAddress:
    return resolveProtoSocketAddress(address.socket_address());
  case envoy::config::core::v3::Address::AddressCase::kPipe:
    return PipeInstance::create(address.pipe().path(), address.pipe().mode());
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
  return absl::InvalidArgumentError("Failed to resolve address:" + address.DebugString());
}

absl::StatusOr<InstanceConstSharedPtr>
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
    return absl::InvalidArgumentError(fmt::format("Unknown address resolver: {}", resolver_name));
  }
  auto instance_or_error = resolver->resolve(socket_address);
  RETURN_IF_NOT_OK_REF(instance_or_error.status());
  return std::move(instance_or_error.value());
}

} // namespace Address
} // namespace Network
} // namespace Envoy
