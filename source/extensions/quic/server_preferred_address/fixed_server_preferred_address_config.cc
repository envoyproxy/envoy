#include "source/extensions/quic/server_preferred_address/fixed_server_preferred_address_config.h"

#include "source/common/network/utility.h"
#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

namespace {

quic::QuicSocketAddress ipOrAddressToAddress(const quic::QuicSocketAddress& address, int32_t port) {
  if (address.port() == 0) {
    return quic::QuicSocketAddress(address.host(), port);
  }

  return address;
}

quic::QuicIpAddress parseIp(const std::string& addr, absl::string_view address_family,
                            const Protobuf::Message& message) {
  quic::QuicIpAddress ip;
  if (!ip.FromString(addr)) {
    ProtoExceptionUtil::throwProtoValidationException(
        absl::StrCat("bad ", address_family, " server preferred address: ", addr), message);
  }
  return ip;
}

quic::QuicSocketAddress parseSocketAddress(const envoy::config::core::v3::SocketAddress& addr,
                                           Network::Address::IpVersion version,
                                           absl::string_view version_str,
                                           const Protobuf::Message& message) {
  // There's no utility to convert from a `SocketAddress`, so wrap it in an `Address` to make use of
  // existing helpers.
  envoy::config::core::v3::Address outer;
  *outer.mutable_socket_address() = addr;
  auto envoy_addr = Network::Utility::protobufAddressToAddress(outer);
  ASSERT(envoy_addr != nullptr,
         "Network::Utility::protobufAddressToAddress throws on failure so this can't be nullptr");
  if (envoy_addr->ip() == nullptr || envoy_addr->ip()->version() != version) {
    ProtoExceptionUtil::throwProtoValidationException(
        absl::StrCat("wrong address type for ", version_str, " server preferred address: ", addr),
        message);
  }

  return envoyIpAddressToQuicSocketAddress(envoy_addr->ip());
}

quic::QuicIpAddress
parseIpAddressFromSocketAddress(const envoy::config::core::v3::SocketAddress& addr,
                                Network::Address::IpVersion version, absl::string_view version_str,
                                const Protobuf::Message& message) {
  auto socket_addr = parseSocketAddress(addr, version, version_str, message);
  if (socket_addr.port() != 0) {
    ProtoExceptionUtil::throwProtoValidationException(
        fmt::format("port must be 0 in this version of Envoy in address '{}'",
                    socket_addr.ToString()),
        message);
  }

  return socket_addr.host();
}

FixedServerPreferredAddressConfig::FamilyAddresses
parseFamily(const std::string& addr_string,
            const envoy::extensions::quic::server_preferred_address::v3::
                FixedServerPreferredAddressConfig::AddressFamilyConfig* addresses,
            Network::Address::IpVersion version, absl::string_view address_family,
            const Protobuf::Message& message) {
  FixedServerPreferredAddressConfig::FamilyAddresses ret;
  if (addresses != nullptr) {
    if (addresses->has_dnat_address() && !addresses->has_address()) {
      ProtoExceptionUtil::throwProtoValidationException(
          absl::StrCat("'dnat_address' but not 'address' is set in server preferred address for ",
                       address_family),
          message);
    }

    if (addresses->has_address()) {
      ret.spa_ = parseSocketAddress(addresses->address(), version, address_family, message);

      if (!addresses->has_dnat_address() && ret.spa_.port() != 0) {
        ProtoExceptionUtil::throwProtoValidationException(
            fmt::format("'address' port must be zero unless 'dnat_address' is set in address {} "
                        "for address family {}",
                        ret.spa_.ToString(), address_family),
            message);
      }
    }

    if (addresses->has_dnat_address()) {
      ret.dnat_ = parseIpAddressFromSocketAddress(addresses->dnat_address(), version,
                                                  address_family, message);
    }
  } else {
    if (!addr_string.empty()) {
      ret.spa_ = quic::QuicSocketAddress(parseIp(addr_string, address_family, message), 0);
    }
  }

  return ret;
}

} // namespace

EnvoyQuicServerPreferredAddressConfig::Addresses
FixedServerPreferredAddressConfig::getServerPreferredAddresses(
    const Network::Address::InstanceConstSharedPtr& local_address) {
  int32_t port = local_address->ip()->port();
  Addresses addresses;
  addresses.ipv4_ = ipOrAddressToAddress(v4_.spa_, port);
  addresses.ipv6_ = ipOrAddressToAddress(v6_.spa_, port);
  addresses.dnat_ipv4_ = quic::QuicSocketAddress(v4_.dnat_, port);
  addresses.dnat_ipv6_ = quic::QuicSocketAddress(v6_.dnat_, port);
  return addresses;
}

Quic::EnvoyQuicServerPreferredAddressConfigPtr
FixedServerPreferredAddressConfigFactory::createServerPreferredAddressConfig(
    const Protobuf::Message& message, ProtobufMessage::ValidationVisitor& validation_visitor,
    ProcessContextOptRef /*context*/) {
  auto& config =
      MessageUtil::downcastAndValidate<const envoy::extensions::quic::server_preferred_address::v3::
                                           FixedServerPreferredAddressConfig&>(message,
                                                                               validation_visitor);

  FixedServerPreferredAddressConfig::FamilyAddresses v4 =
      parseFamily(config.ipv4_address(), config.has_ipv4_config() ? &config.ipv4_config() : nullptr,
                  Network::Address::IpVersion::v4, "v4", message);
  FixedServerPreferredAddressConfig::FamilyAddresses v6 =
      parseFamily(config.ipv6_address(), config.has_ipv6_config() ? &config.ipv6_config() : nullptr,
                  Network::Address::IpVersion::v6, "v6", message);

  return std::make_unique<FixedServerPreferredAddressConfig>(v4, v6);
}

REGISTER_FACTORY(FixedServerPreferredAddressConfigFactory,
                 EnvoyQuicServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy
