#include "source/extensions/quic/server_preferred_address/datasource_server_preferred_address_config.h"

#include "envoy/common/exception.h"

#include "source/common/common/utility.h"
#include "source/common/config/datasource.h"
#include "source/common/network/utility.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/extensions/quic/server_preferred_address/server_preferred_address.h"

namespace Envoy {
namespace Quic {

namespace {

quic::QuicIpAddress parseIp(const envoy::config::core::v3::DataSource& source,
                            quiche::IpAddressFamily address_family,
                            absl::string_view address_family_str, const Protobuf::Message& message,
                            Server::Configuration::ServerFactoryContext& context) {
  std::string data =
      THROW_OR_RETURN_VALUE(Config::DataSource::read(source, false, context.api()), std::string);

  quic::QuicIpAddress ip;
  if (!ip.FromString(std::string(StringUtil::trim(data)))) {
    ProtoExceptionUtil::throwProtoValidationException(
        absl::StrCat("bad ", address_family_str, " server preferred address: ", data), message);
  }

  if (ip.address_family() != address_family) {
    ProtoExceptionUtil::throwProtoValidationException(
        absl::StrCat("wrong address family for ", address_family_str,
                     " server preferred address: ", data),
        message);
  }
  return ip;
}

ServerPreferredAddressConfig::IpVersionConfig
parseFamily(const envoy::extensions::quic::server_preferred_address::v3::
                DataSourceServerPreferredAddressConfig::AddressFamilyConfig& addresses,
            quiche::IpAddressFamily address_family, absl::string_view address_family_str,
            const Protobuf::Message& message,
            Server::Configuration::ServerFactoryContext& context) {
  ServerPreferredAddressConfig::IpVersionConfig ret;

  const quic::QuicIpAddress spa_addr =
      parseIp(addresses.address(), address_family, address_family_str, message, context);

  if (!addresses.has_dnat_address() && addresses.has_port()) {
    ProtoExceptionUtil::throwProtoValidationException(
        fmt::format("port must be unset unless 'dnat_address' is set "
                    "for address family {}",
                    address_family_str),
        message);
  }

  uint16_t spa_port = 0;
  if (addresses.has_port()) {
    std::string port_str = THROW_OR_RETURN_VALUE(
        Config::DataSource::read(addresses.port(), false, context.api()), std::string);

    // absl::SimpleAtoi doesn't work with uint16_t, so first convert to uint32_t.
    uint32_t big_port = 0;
    const bool success = absl::SimpleAtoi(port_str, &big_port);
    if (!success || big_port > UINT16_MAX) {
      ProtoExceptionUtil::throwProtoValidationException(
          absl::StrCat("server preferred address ", address_family_str,
                       " port was not a valid port: ", port_str),
          message);
    }

    spa_port = big_port;
  }
  ret.spa_ = quic::QuicSocketAddress(spa_addr, spa_port);

  if (addresses.has_dnat_address()) {
    ret.dnat_ =
        parseIp(addresses.dnat_address(), address_family, address_family_str, message, context);
  }

  return ret;
}

} // namespace

Quic::EnvoyQuicServerPreferredAddressConfigPtr
DataSourceServerPreferredAddressConfigFactory::createServerPreferredAddressConfig(
    const Protobuf::Message& message, ProtobufMessage::ValidationVisitor& validation_visitor,
    Server::Configuration::ServerFactoryContext& context) {
  auto& config =
      MessageUtil::downcastAndValidate<const envoy::extensions::quic::server_preferred_address::v3::
                                           DataSourceServerPreferredAddressConfig&>(
          message, validation_visitor);

  ServerPreferredAddressConfig::IpVersionConfig v4;
  if (config.has_ipv4_config()) {
    v4 = parseFamily(config.ipv4_config(), quiche::IpAddressFamily::IP_V4, "v4", message, context);
  }

  ServerPreferredAddressConfig::IpVersionConfig v6;
  if (config.has_ipv6_config()) {
    v6 = parseFamily(config.ipv6_config(), quiche::IpAddressFamily::IP_V6, "v6", message, context);
  }

  return std::make_unique<ServerPreferredAddressConfig>(v4, v6);
}

REGISTER_FACTORY(DataSourceServerPreferredAddressConfigFactory,
                 Quic::EnvoyQuicServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy
