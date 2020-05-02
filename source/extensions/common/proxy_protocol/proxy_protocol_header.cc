#include "extensions/common/proxy_protocol/proxy_protocol_header.h"

#include <sstream>

#include "envoy/buffer/buffer.h"
#include "envoy/network/address.h"

#include "common/network/address_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ProxyProtocol {

void generateV1Header(const std::string& src_addr, const std::string& dst_addr, uint32_t src_port,
                      uint32_t dst_port, Network::Address::IpVersion ip_version,
                      Buffer::Instance& out) {
  std::ostringstream stream;
  stream << PROXY_PROTO_V1_SIGNATURE;

  switch (ip_version) {
  case Network::Address::IpVersion::v4:
    stream << PROXY_PROTO_V1_AF_INET << " ";
    break;
  case Network::Address::IpVersion::v6:
    stream << PROXY_PROTO_V1_AF_INET6 << " ";
    break;
  }

  stream << src_addr << " ";
  stream << dst_addr << " ";
  stream << src_port << " ";
  stream << dst_port << "\r\n";

  out.add(stream.str());
}

void generateV1Header(const Network::Address::Ip& source_address,
                      const Network::Address::Ip& dest_address, Buffer::Instance& out) {
  generateV1Header(source_address.addressAsString(), dest_address.addressAsString(),
                   source_address.port(), dest_address.port(), source_address.version(), out);
}

void generateV2Header(const std::string& src_addr, const std::string& dst_addr, uint32_t src_port,
                      uint32_t dst_port, Network::Address::IpVersion ip_version,
                      Buffer::Instance& out) {
  out.add(PROXY_PROTO_V2_SIGNATURE, PROXY_PROTO_V2_SIGNATURE_LEN);

  const uint8_t version_and_command = PROXY_PROTO_V2_VERSION << 4 | PROXY_PROTO_V2_ONBEHALF_OF;
  out.add(&version_and_command, 1);

  uint8_t address_family_and_protocol;
  switch (ip_version) {
  case Network::Address::IpVersion::v4:
    address_family_and_protocol = PROXY_PROTO_V2_AF_INET << 4;
    break;
  case Network::Address::IpVersion::v6:
    address_family_and_protocol = PROXY_PROTO_V2_AF_INET6 << 4;
    break;
  }
  address_family_and_protocol |= PROXY_PROTO_V2_TRANSPORT_STREAM;
  out.add(&address_family_and_protocol, 1);

  uint8_t addr_length[2]{0, 0};
  switch (ip_version) {
  case Network::Address::IpVersion::v4: {
    addr_length[1] = PROXY_PROTO_V2_ADDR_LEN_INET;
    out.add(addr_length, 2);

    uint8_t addrs[8];
    const auto net_src_addr =
        Network::Address::Ipv4Instance(src_addr, src_port).ip()->ipv4()->address();
    const auto net_dst_addr =
        Network::Address::Ipv4Instance(dst_addr, dst_port).ip()->ipv4()->address();
    memcpy(addrs, &net_src_addr, 4);
    memcpy(&addrs[4], &net_dst_addr, 4);
    out.add(addrs, 8);
    break;
  }
  case Network::Address::IpVersion::v6: {
    addr_length[1] = PROXY_PROTO_V2_ADDR_LEN_INET6;
    out.add(addr_length, 2);

    uint8_t addrs[32];
    const auto net_src_addr =
        Network::Address::Ipv6Instance(src_addr, src_port).ip()->ipv6()->address();
    const auto net_dst_addr =
        Network::Address::Ipv6Instance(dst_addr, dst_port).ip()->ipv6()->address();
    memcpy(addrs, &net_src_addr, 16);
    memcpy(&addrs[16], &net_dst_addr, 16);
    out.add(addrs, 32);
    break;
  }
  }

  uint8_t ports[4];
  const auto net_src_port = htons(static_cast<uint16_t>(src_port));
  const auto net_dst_port = htons(static_cast<uint16_t>(dst_port));
  memcpy(ports, &net_src_port, 2);
  memcpy(&ports[2], &net_dst_port, 2);
  out.add(ports, 4);
}

void generateV2Header(const Network::Address::Ip& source_address,
                      const Network::Address::Ip& dest_address, Buffer::Instance& out) {
  generateV2Header(source_address.addressAsString(), dest_address.addressAsString(),
                   source_address.port(), dest_address.port(), source_address.version(), out);
}

void generateProxyProtoHeader(const envoy::config::core::v3::ProxyProtocolConfig& config,
                              const Network::Connection& connection, Buffer::Instance& out) {
  const Network::Address::Ip& dest_address = *connection.localAddress()->ip();
  const Network::Address::Ip& source_address = *connection.remoteAddress()->ip();
  if (config.version() == envoy::config::core::v3::ProxyProtocolConfig::V1) {
    generateV1Header(source_address, dest_address, out);
  } else if (config.version() == envoy::config::core::v3::ProxyProtocolConfig::V2) {
    generateV2Header(source_address, dest_address, out);
  }
}

void generateV2LocalHeader(Buffer::Instance& out) {
  out.add(PROXY_PROTO_V2_SIGNATURE, PROXY_PROTO_V2_SIGNATURE_LEN);
  const uint8_t addr_fam_protocol_and_length[4]{PROXY_PROTO_V2_VERSION << 4, 0, 0, 0};
  out.add(addr_fam_protocol_and_length, 4);
}

} // namespace ProxyProtocol
} // namespace Common
} // namespace Extensions
} // namespace Envoy
