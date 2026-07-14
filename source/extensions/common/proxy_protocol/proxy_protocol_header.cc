#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

#include <sstream>

#include "envoy/buffer/buffer.h"
#include "envoy/network/address.h"

#include "source/common/network/address_impl.h"
#include "source/common/runtime/runtime_features.h"

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

namespace {
uint32_t v2AddrStructLen(Network::Address::IpVersion ip_version) {
  switch (ip_version) {
  case Network::Address::IpVersion::v4:
    return PROXY_PROTO_V2_ADDR_LEN_INET;
  case Network::Address::IpVersion::v6:
    return PROXY_PROTO_V2_ADDR_LEN_INET6;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}
} // namespace

void generateV2Header(const std::string& src_addr, const std::string& dst_addr, uint32_t src_port,
                      uint32_t dst_port, Network::Address::IpVersion ip_version,
                      uint16_t extension_length, Buffer::Instance& out) {
  const uint32_t addr_struct_len = v2AddrStructLen(ip_version);
  const uint32_t addr_length = addr_struct_len + static_cast<uint32_t>(extension_length);
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.proxy_protocol_remove_too_long_tlvs") &&
      addr_length > UINT16_MAX) {
    // Emit a self-consistent but spec-invalid v2 header so the peer drops the connection instead of
    // incorrect framing on the wire. Length is zero so the framing remains consistent regardless of
    // what the caller appends.
    //
    // TODO(ggreenway): propagate this failure to the transport socket and fail the
    // connection instead of writing an invalid header.
    out.add(PROXY_PROTO_V2_SIGNATURE, PROXY_PROTO_V2_SIGNATURE_LEN);
    constexpr uint8_t bad_cmd = 0xF;
    const uint8_t bad_ver_cmd = (PROXY_PROTO_V2_VERSION << 4) | bad_cmd;
    out.add(&bad_ver_cmd, 1);
    const uint8_t af_proto = 0;
    out.add(&af_proto, 1);
    const uint16_t zero_len_n = 0;
    out.add(&zero_len_n, 2);
    IS_ENVOY_BUG(
        fmt::format("v2 PROXY protocol header length would overflow uint16_t: {}", addr_length));
    return;
  }

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

  const uint16_t addr_length_n = htons(static_cast<uint16_t>(addr_length));
  out.add(&addr_length_n, 2);

  switch (ip_version) {
  case Network::Address::IpVersion::v4: {
    const uint32_t net_src_addr =
        Network::Address::Ipv4Instance(src_addr, src_port).ip()->ipv4()->address();
    const uint32_t net_dst_addr =
        Network::Address::Ipv4Instance(dst_addr, dst_port).ip()->ipv4()->address();
    out.add(&net_src_addr, 4);
    out.add(&net_dst_addr, 4);
    break;
  }
  case Network::Address::IpVersion::v6: {
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
                      bool pass_all_tlvs, const absl::flat_hash_set<uint8_t>& pass_through_tlvs,
                      const std::vector<Envoy::Network::ProxyProtocolTLV>& custom_tlvs) {
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

  // The wire `len` field covers the address struct (addresses + ports) plus the TLVs,
  // and must fit in uint16_t. Reserve room for the address struct when capping TLVs.
  const uint32_t addr_struct_len = v2AddrStructLen(src.version());
  const bool remove_too_long_tlvs = Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.proxy_protocol_remove_too_long_tlvs");
  const uint64_t max_extension_length =
      remove_too_long_tlvs ? (UINT16_MAX - addr_struct_len) : UINT16_MAX;

  std::vector<Envoy::Network::ProxyProtocolTLV> combined_tlv_vector;
  std::vector<Envoy::Network::ProxyProtocolTLV> final_tlvs;
  combined_tlv_vector.reserve(custom_tlvs.size() + proxy_proto_data.tlv_vector_.size());

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.proxy_protocol_allow_duplicate_tlvs")) {
    absl::flat_hash_set<uint8_t> config_specified_types;
    for (const auto& tlv : custom_tlvs) {
      combined_tlv_vector.emplace_back(tlv);
      config_specified_types.insert(tlv.type);
    }

    // Combine TLVs from the proxy_proto_data with the custom TLVs.
    for (const auto& tlv : proxy_proto_data.tlv_vector_) {
      if (!pass_all_tlvs && !pass_through_tlvs.contains(tlv.type)) {
        // Skip any TLV that is not in the set of passthrough TLVs.
        continue;
      }
      if (!config_specified_types.contains(tlv.type)) {
        combined_tlv_vector.emplace_back(tlv);
      }
    }
  } else {
    absl::flat_hash_set<uint8_t> seen_types;
    for (const auto& tlv : custom_tlvs) {
      ASSERT(!seen_types.contains(tlv.type));
      combined_tlv_vector.emplace_back(tlv);
      seen_types.insert(tlv.type);
    }

    // Combine TLVs from the proxy_proto_data with the custom TLVs.
    for (const auto& tlv : proxy_proto_data.tlv_vector_) {
      if (!pass_all_tlvs && !pass_through_tlvs.contains(tlv.type)) {
        // Skip any TLV that is not in the set of passthrough TLVs.
        continue;
      }
      if (seen_types.contains(tlv.type)) {
        // Skip any duplicate TLVs from being added to the combined TLV vector.
        ENVOY_LOG_EVERY_POW_2_MISC(info, "Skipping duplicate TLV type {}", tlv.type);
        continue;
      }
      seen_types.insert(tlv.type);
      combined_tlv_vector.emplace_back(tlv);
    }
  }

  // Filter out TLVs that would push the total v2 `len` field past 65535.
  uint64_t extension_length = 0;
  bool skipped_tlvs = false;
  for (auto&& tlv : combined_tlv_vector) {
    uint64_t new_size = extension_length + PROXY_PROTO_V2_TLV_TYPE_LENGTH_LEN + tlv.value.size();
    if (new_size > max_extension_length) {
      ENVOY_LOG_MISC(warn, "Skipping TLV type {} because adding it would exceed the 65535 limit.",
                     tlv.type);
      skipped_tlvs = true;
      continue;
    }
    extension_length = new_size;
    final_tlvs.push_back(tlv);
  }

  generateV2Header(src.addressAsString(), dst.addressAsString(), src.port(), dst.port(),
                   src.version(), static_cast<uint16_t>(extension_length), out);

  const std::vector<Envoy::Network::ProxyProtocolTLV>& really_final_tlvs =
      remove_too_long_tlvs ? final_tlvs : combined_tlv_vector;
  for (auto&& tlv : really_final_tlvs) {
    out.add(&tlv.type, 1);
    uint16_t size = htons(static_cast<uint16_t>(tlv.value.size()));
    out.add(&size, sizeof(uint16_t));
    out.add(&tlv.value.front(), tlv.value.size());
  }

  // return true if no TLVs were skipped, otherwise false to increment the counter
  // in the upstream proxy protocol transport socket stats.
  return !skipped_tlvs;
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
