#pragma once
#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

// See https://github.com/haproxy/haproxy/blob/master/doc/proxy-protocol.txt for definitions

constexpr char PROXY_PROTO_V1_SIGNATURE[] = "PROXY ";
constexpr uint32_t PROXY_PROTO_V1_SIGNATURE_LEN = 6;
constexpr char PROXY_PROTO_V2_SIGNATURE[] = "\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a";
constexpr uint32_t PROXY_PROTO_V2_SIGNATURE_LEN = 12;
constexpr uint32_t PROXY_PROTO_V2_HEADER_LEN = 16;
constexpr uint32_t PROXY_PROTO_V2_VERSION = 0x2;
constexpr uint32_t PROXY_PROTO_V2_ONBEHALF_OF = 0x1;
constexpr uint32_t PROXY_PROTO_V2_LOCAL = 0x0;

constexpr uint32_t PROXY_PROTO_V2_AF_INET = 0x1;
constexpr uint32_t PROXY_PROTO_V2_AF_INET6 = 0x2;
constexpr uint32_t PROXY_PROTO_V2_AF_UNIX = 0x3;

struct WireHeader {
  WireHeader(size_t extensions_length)
      : extensions_length_(extensions_length), protocol_version_(Network::Address::IpVersion::v4),
        remote_address_(0), local_address_(0), local_command_(true) {}
  WireHeader(size_t extensions_length, Network::Address::IpVersion protocol_version,
             Network::Address::InstanceConstSharedPtr remote_address,
             Network::Address::InstanceConstSharedPtr local_address)
      : extensions_length_(extensions_length), protocol_version_(protocol_version),
        remote_address_(remote_address), local_address_(local_address), local_command_(false) {

    ASSERT(extensions_length_ <= 65535);
  }
  size_t extensions_length_;
  const Network::Address::IpVersion protocol_version_;
  const Network::Address::InstanceConstSharedPtr remote_address_;
  const Network::Address::InstanceConstSharedPtr local_address_;
  const bool local_command_;
};

constexpr uint32_t PROXY_PROTO_V2_ADDR_LEN_UNSPEC = 0;
constexpr uint32_t PROXY_PROTO_V2_ADDR_LEN_INET = 12;
constexpr uint32_t PROXY_PROTO_V2_ADDR_LEN_INET6 = 36;
constexpr uint32_t PROXY_PROTO_V2_ADDR_LEN_UNIX = 216;

constexpr uint8_t PROXY_PROTO_V2_TRANSPORT_STREAM = 0x1;
constexpr uint8_t PROXY_PROTO_V2_TRANSPORT_DGRAM = 0x2;

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
