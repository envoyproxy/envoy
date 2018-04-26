#include "common/network/resolver_impl.h"

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/common/exception.h"
#include "envoy/network/address.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Implementation of a resolver for IP addresses.
 */
class IpResolver : public Resolver {

public:
  InstanceConstSharedPtr
  resolve(const envoy::api::v2::core::SocketAddress& socket_address) override {
    switch (socket_address.port_specifier_case()) {
    case envoy::api::v2::core::SocketAddress::kPortValue:
    // Default to port 0 if no port value is specified.
    case envoy::api::v2::core::SocketAddress::PORT_SPECIFIER_NOT_SET:
      return Network::Utility::parseInternetAddress(
          socket_address.address(), socket_address.port_value(), !socket_address.ipv4_compat());

    default:
      throw EnvoyException(fmt::format("IP resolver can't handle port specifier type {}",
                                       socket_address.port_specifier_case()));
    }
  }

  InstanceRangeConstSharedPtr
  resolve(const envoy::api::v2::core::SocketAddressPortRange& socket_address_port_range) override {
    switch (socket_address_port_range.port_specifier_case()) {
    case envoy::api::v2::core::SocketAddressPortRange::kPortValue:
    // Default to port 0 if no port value is specified.
    case envoy::api::v2::core::SocketAddressPortRange::PORT_SPECIFIER_NOT_SET: {
      InstanceConstSharedPtr instance = Network::Utility::parseInternetAddress(
          socket_address_port_range.address(), socket_address_port_range.port_value(),
          !socket_address_port_range.ipv4_compat());
      // TODO(rdsmith): Assertion of some kind that this is an IP instance?
      switch (instance->ip()->version()) {
      case IpVersion::v4:
        return std::make_shared<Ipv4InstanceRange>(instance->ip()->addressAsString(),
                                                   instance->ip()->port(),
                                                   instance->ip()->port());
      case IpVersion::v6:
        return std::make_shared<Ipv6InstanceRange>(instance->ip()->addressAsString(),
                                                   instance->ip()->port(),
                                                   instance->ip()->port());
      }
    }
    case envoy::api::v2::core::SocketAddressPortRange::kPortRange: {
      uint32_t starting_port = socket_address_port_range.port_range().port_value_start();
      uint32_t ending_port = socket_address_port_range.port_range().port_value_end();
      if (ending_port < starting_port)  {
        throw EnvoyException(
            fmt::format("IP resolver given port range with end before start: [{}, {}]",
                        starting_port, ending_port));
      }
      if (static_cast<in_port_t>(ending_port) != ending_port) {
        throw EnvoyException(
            fmt::format("IP resolver given port to large for in_port_t: {}", ending_port));
      }

      InstanceConstSharedPtr instance = Network::Utility::parseInternetAddress(
          socket_address_port_range.address(), starting_port,
          !socket_address_port_range.ipv4_compat());
      // TODO(rdsmith): Assertion of some kind that this is an IP instance?
      switch (instance->ip()->version()) {
      case IpVersion::v4:
        return std::make_shared<Ipv4InstanceRange>(instance->ip()->addressAsString(),
                                                   starting_port, ending_port);
      case IpVersion::v6:
        return std::make_shared<Ipv6InstanceRange>(instance->ip()->addressAsString(),
                                                   starting_port, ending_port);
      }
    }

    default:
      throw EnvoyException(fmt::format("IP resolver can't handle port specifier type {}",
                                       socket_address_port_range.port_specifier_case()));
    }
  }

  std::string name() const override { return Config::AddressResolverNames::get().IP; }
};

/**
 * Static registration for the IP resolver. @see RegisterFactory.
 */
static Registry::RegisterFactory<IpResolver, Resolver> ip_registered_;

InstanceConstSharedPtr resolveProtoAddress(const envoy::api::v2::core::Address& address) {
  switch (address.address_case()) {
  case envoy::api::v2::core::Address::kSocketAddress:
    return resolveProtoSocketAddress(address.socket_address());
  case envoy::api::v2::core::Address::kPipe:
    return InstanceConstSharedPtr{new PipeInstance(address.pipe().path())};
  default:
    throw EnvoyException("Address must be a socket or pipe: " + address.DebugString());
  }
}

InstanceConstSharedPtr
resolveProtoSocketAddress(const envoy::api::v2::core::SocketAddress& socket_address) {
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

InstanceRangeConstSharedPtr
resolveProtoSocketAddress(
    const envoy::api::v2::core::SocketAddressPortRange& socket_address_port_range) {
  Resolver* resolver = nullptr;
  const std::string& resolver_name = socket_address_port_range.resolver_name();
  if (resolver_name.empty()) {
    resolver =
        Registry::FactoryRegistry<Resolver>::getFactory(Config::AddressResolverNames::get().IP);
  } else {
    resolver = Registry::FactoryRegistry<Resolver>::getFactory(resolver_name);
  }
  if (resolver == nullptr) {
    throw EnvoyException(fmt::format("Unknown address resolver: {}", resolver_name));
  }
  return resolver->resolve(socket_address_port_range);
}

} // namespace Address
} // namespace Network
} // namespace Envoy
