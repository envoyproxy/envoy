#include "source/extensions/quic/server_preferred_address/fixed_server_preferred_address_config.h"

#include "source/common/network/utility.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/extensions/quic/server_preferred_address/server_preferred_address.h"

namespace Envoy {
namespace Quic {

namespace {

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
  auto envoy_addr = Network::Utility::protobufAddressToAddressNoThrow(outer);
  if (!envoy_addr) {
    ProtoExceptionUtil::throwProtoValidationException(absl::StrCat("Invalid address ", outer),
                                                      message);
  }
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

ServerPreferredAddressConfig::IpVersionConfig
parseFamily(const std::string& addr_string,
            const envoy::extensions::quic::server_preferred_address::v3::
                FixedServerPreferredAddressConfig::AddressFamilyConfig* addresses,
            Network::Address::IpVersion version, absl::string_view address_family,
            const Protobuf::Message& message) {
  ServerPreferredAddressConfig::IpVersionConfig ret;
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

Quic::EnvoyQuicServerPreferredAddressConfigPtr
FixedServerPreferredAddressConfigFactory::createServerPreferredAddressConfig(
    const Protobuf::Message& message, ProtobufMessage::ValidationVisitor& validation_visitor,
    Server::Configuration::ServerFactoryContext& /*context*/) {
  auto& config =
      MessageUtil::downcastAndValidate<const envoy::extensions::quic::server_preferred_address::v3::
                                           FixedServerPreferredAddressConfig&>(message,
                                                                               validation_visitor);

  ServerPreferredAddressConfig::IpVersionConfig v4 =
      parseFamily(config.ipv4_address(), config.has_ipv4_config() ? &config.ipv4_config() : nullptr,
                  Network::Address::IpVersion::v4, "v4", message);
  ServerPreferredAddressConfig::IpVersionConfig v6 =
      parseFamily(config.ipv6_address(), config.has_ipv6_config() ? &config.ipv6_config() : nullptr,
                  Network::Address::IpVersion::v6, "v6", message);

  return std::make_unique<ServerPreferredAddressConfig>(v4, v6);
}

REGISTER_FACTORY(FixedServerPreferredAddressConfigFactory,
                 EnvoyQuicServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy
