#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

#include <sstream>

#include "envoy/buffer/buffer.h"
#include "envoy/network/address.h"

#include "source/common/network/address_impl.h"

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
                      uint16_t extension_length, Buffer::Instance& out) {
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

  // Number of following bytes part of the header in V2 protocol.
  uint16_t addr_length;
  uint16_t addr_length_n; // Network byte order

  switch (ip_version) {
  case Network::Address::IpVersion::v4: {
    addr_length = PROXY_PROTO_V2_ADDR_LEN_INET + extension_length;
    addr_length_n = htons(addr_length);
    out.add(&addr_length_n, 2);
    const uint32_t net_src_addr =
        Network::Address::Ipv4Instance(src_addr, src_port).ip()->ipv4()->address();
    const uint32_t net_dst_addr =
        Network::Address::Ipv4Instance(dst_addr, dst_port).ip()->ipv4()->address();
    out.add(&net_src_addr, 4);
    out.add(&net_dst_addr, 4);
    break;
  }
  case Network::Address::IpVersion::v6: {
    addr_length = PROXY_PROTO_V2_ADDR_LEN_INET6 + extension_length;
    addr_length_n = htons(addr_length);
    out.add(&addr_length_n, 2);
    const absl::uint128 net_src_addr =
        Network::Address::Ipv6Instance(src_addr, src_port).ip()->ipv6()->address();
    const absl::uint128 net_dst_addr =
        Network::Address::Ipv6Instance(dst_addr, dst_port).ip()->ipv6()->address();
    out.add(&net_src_addr, 16);
    out.add(&net_dst_addr, 16);
    break;
  }
  }

  const uint16_t net_src_port = htons(static_cast<uint16_t>(src_port));
  const uint16_t net_dst_port = htons(static_cast<uint16_t>(dst_port));
  out.add(&net_src_port, 2);
  out.add(&net_dst_port, 2);
}

void generateV2Header(const std::string& src_addr, const std::string& dst_addr, uint32_t src_port,
                      uint32_t dst_port, Network::Address::IpVersion ip_version,
                      Buffer::Instance& out) {
  generateV2Header(src_addr, dst_addr, src_port, dst_port, ip_version, 0, out);
}

void generateV2Header(const Network::Address::Ip& source_address,
                      const Network::Address::Ip& dest_address, Buffer::Instance& out) {
  generateV2Header(source_address.addressAsString(), dest_address.addressAsString(),
                   source_address.port(), dest_address.port(), source_address.version(), 0, out);
}

bool generateV2Header(const Network::ProxyProtocolData& proxy_proto_data, Buffer::Instance& out,
                      bool pass_all_tlvs, const absl::flat_hash_set<uint8_t>& pass_through_tlvs) {
  uint64_t extension_length = 0;
  for (auto&& tlv : proxy_proto_data.tlv_vector_) {
    if (!pass_all_tlvs && !pass_through_tlvs.contains(tlv.type)) {
      continue;
    }
    extension_length += PROXY_PROTO_V2_TLV_TYPE_LENGTH_LEN + tlv.value.size();
    if (extension_length > std::numeric_limits<uint16_t>::max()) {
      ENVOY_LOG_MISC(
          warn, "Generating Proxy Protocol V2 header: TLVs exceed length limit {}, already got {}",
          std::numeric_limits<uint16_t>::max(), extension_length);
      return false;
    }
  }

  ASSERT(extension_length <= std::numeric_limits<uint16_t>::max());
  if (proxy_proto_data.src_addr_ == nullptr || proxy_proto_data.src_addr_->ip() == nullptr) {
    IS_ENVOY_BUG("Missing or incorrect source IP in proxy_proto_data_");
    return false;
  }
  if (proxy_proto_data.dst_addr_ == nullptr || proxy_proto_data.dst_addr_->ip() == nullptr) {
    IS_ENVOY_BUG("Missing or incorrect dest IP in proxy_proto_data_");
    return false;
  }

  const auto& src = *proxy_proto_data.src_addr_->ip();
  const auto& dst = *proxy_proto_data.dst_addr_->ip();
  generateV2Header(src.addressAsString(), dst.addressAsString(), src.port(), dst.port(),
                   src.version(), static_cast<uint16_t>(extension_length), out);

  // Generate the TLV vector.
  for (auto&& tlv : proxy_proto_data.tlv_vector_) {
    if (!pass_all_tlvs && !pass_through_tlvs.contains(tlv.type)) {
      continue;
    }
    out.add(&tlv.type, 1);
    uint16_t size = htons(static_cast<uint16_t>(tlv.value.size()));
    out.add(&size, sizeof(uint16_t));
    out.add(&tlv.value.front(), tlv.value.size());
  }
  return true;
}

void generateProxyProtoHeader(const envoy::config::core::v3::ProxyProtocolConfig& config,
                              const Network::Connection& connection, Buffer::Instance& out) {
  const Network::Address::Ip& dest_address =
      *connection.connectionInfoProvider().localAddress()->ip();
  const Network::Address::Ip& source_address =
      *connection.connectionInfoProvider().remoteAddress()->ip();
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
