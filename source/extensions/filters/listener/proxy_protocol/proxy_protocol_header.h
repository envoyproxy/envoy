#pragma once

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

//
// See https://github.com/haproxy/haproxy/blob/master/doc/proxy-protocol.txt
// for definitions

constexpr char PP1_SIGNATURE[] = "PROXY ";
constexpr uint32_t PP1_SIGNATURE_LEN = 6;
constexpr char PP2_SIGNATURE[] = "\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a";
constexpr uint32_t PP2_SIGNATURE_LEN = 12;
constexpr uint32_t PP2_HEADER_LEN = 16;
constexpr uint32_t PP2_VERSION = 0x2;
constexpr uint32_t PP2_ONBEHALF_OF = 0x1;
constexpr uint32_t PP2_LOCAL = 0x0;

constexpr uint32_t PP2_AF_INET = 0x1;
constexpr uint32_t PP2_AF_INET6 = 0x2;
constexpr uint32_t PP2_AF_UNIX = 0x3;

struct PpHeader {
  bool valid;
  Network::Address::IpVersion protocol_version;
  Network::Address::InstanceConstSharedPtr remote_address;
  Network::Address::InstanceConstSharedPtr local_address;
};

constexpr uint32_t PP2_ADDR_LEN_UNSPEC = 0;
constexpr uint32_t PP2_ADDR_LEN_INET = 12;
constexpr uint32_t PP2_ADDR_LEN_INET6 = 40;
constexpr uint32_t PP2_ADDR_LEN_UNIX = 216;

constexpr uint8_t PP2_TP_STREAM = 0x1;
constexpr uint8_t PP2_TP_DGRAM = 0x2;

constexpr uint32_t PP2_HDR_LEN_UNSPEC = (PP2_HEADER_LEN + PP2_ADDR_LEN_UNSPEC);
constexpr uint32_t PP2_HDR_LEN_INET = (PP2_HEADER_LEN + PP2_ADDR_LEN_INET);
constexpr uint32_t PP2_HDR_LEN_INET6 = (PP2_HEADER_LEN + PP2_ADDR_LEN_INET6);
constexpr uint32_t PP2_HDR_LEN_UNIX = (PP2_HEADER_LEN + PP2_ADDR_LEN_UNIX);

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
