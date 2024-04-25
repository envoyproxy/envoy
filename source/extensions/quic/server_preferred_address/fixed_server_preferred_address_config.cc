#include "source/extensions/quic/server_preferred_address/fixed_server_preferred_address_config.h"

#include "source/common/network/utility.h"
#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

namespace {

quic::QuicSocketAddress
ipOrAddressToAddress(const FixedServerPreferredAddressConfig::QuicSocketOrIpAddress& address,
                     int32_t port) {
  return absl::visit(
      [&](const auto& arg) -> quic::QuicSocketAddress {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, quic::QuicSocketAddress>) {
          return arg;
        }

        if constexpr (std::is_same_v<T, quic::QuicIpAddress>) {
          return quic::QuicSocketAddress(arg, port);
        }

        IS_ENVOY_BUG(fmt::format("Unhandled type in variant visitor: {}", address.index()));
        return {};
      },
      address);
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
  if (envoy_addr == nullptr) {
    ProtoExceptionUtil::throwProtoValidationException(
        absl::StrCat("bad ", version_str, " server preferred address: ", addr), message);
  }
  if (envoy_addr->ip() == nullptr || envoy_addr->ip()->version() != version) {
    ProtoExceptionUtil::throwProtoValidationException(
        absl::StrCat("wrong address type for ", version_str, " server preferred address: ", addr),
        message);
  }

  return envoyIpAddressToQuicSocketAddress(envoy_addr->ip());
}

} // namespace

EnvoyQuicServerPreferredAddressConfig::Addresses
FixedServerPreferredAddressConfig::getServerPreferredAddresses(
    const Network::Address::InstanceConstSharedPtr& local_address) {
  int32_t port = local_address->ip()->port();
  Addresses addresses;
  addresses.ipv4_ = ipOrAddressToAddress(ip_v4_, port);
  addresses.ipv6_ = ipOrAddressToAddress(ip_v6_, port);
  addresses.dnat_ipv4_ = quic::QuicSocketAddress(dnat_ip_v4_, port);
  addresses.dnat_ipv6_ = quic::QuicSocketAddress(dnat_ip_v6_, port);
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
  FixedServerPreferredAddressConfig::QuicSocketOrIpAddress ip_v4, ip_v6;
  switch (config.ipv4_type_case()) {
  case envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig::
      kIpv4Address:
    ip_v4 = parseIp(config.ipv4_address(), "v4", message);
    break;
  case envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig::
      kIpv4AddressAndPort:
    ip_v4 = parseSocketAddress(config.ipv4_address_and_port(), Network::Address::IpVersion::v4,
                               "v4", message);
    break;
  case envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig::
      IPV4_TYPE_NOT_SET:
    break;
  }

  switch (config.ipv6_type_case()) {
  case envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig::
      kIpv6Address:
    ip_v6 = parseIp(config.ipv6_address(), "v6", message);
    break;
  case envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig::
      kIpv6AddressAndPort:
    ip_v6 = parseSocketAddress(config.ipv6_address_and_port(), Network::Address::IpVersion::v6,
                               "v6", message);
    break;
  case envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig::
      IPV6_TYPE_NOT_SET:
    break;
  }

  quic::QuicIpAddress dnat_v4, dnat_v6;
  if (config.has_ipv4_dnat_address()) {
    dnat_v4 = parseIp(config.ipv4_dnat_address(), "dnat v4", message);
  }

  if (config.has_ipv6_dnat_address()) {
    dnat_v6 = parseIp(config.ipv6_dnat_address(), "dnat v6", message);
  }

  return std::make_unique<FixedServerPreferredAddressConfig>(ip_v4, ip_v6, dnat_v4, dnat_v6);
}

REGISTER_FACTORY(FixedServerPreferredAddressConfigFactory,
                 EnvoyQuicServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy
