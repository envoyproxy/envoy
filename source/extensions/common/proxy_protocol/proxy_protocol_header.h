#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ProxyProtocol {

// See https://github.com/haproxy/haproxy/blob/master/doc/proxy-protocol.txt for definitions

constexpr char PROXY_PROTO_V1_SIGNATURE[] = "PROXY ";
constexpr auto PROXY_PROTO_V1_AF_INET = "TCP4";
constexpr auto PROXY_PROTO_V1_AF_INET6 = "TCP6";
constexpr auto PROXY_PROTO_V1_UNKNOWN = "UNKNOWN";

constexpr char PROXY_PROTO_V2_SIGNATURE[] = "\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a";

constexpr uint32_t PROXY_PROTO_V1_SIGNATURE_LEN = 6;
constexpr uint32_t PROXY_PROTO_V2_SIGNATURE_LEN = 12;
constexpr uint32_t PROXY_PROTO_V2_HEADER_LEN = 16;

constexpr uint32_t PROXY_PROTO_V2_VERSION = 0x2;
constexpr uint32_t PROXY_PROTO_V2_ONBEHALF_OF = 0x1;
constexpr uint32_t PROXY_PROTO_V2_LOCAL = 0x0;

constexpr uint32_t PROXY_PROTO_V2_AF_INET = 0x1;
constexpr uint32_t PROXY_PROTO_V2_AF_INET6 = 0x2;
constexpr uint32_t PROXY_PROTO_V2_AF_UNIX = 0x3;

constexpr uint8_t PROXY_PROTO_V2_TRANSPORT_STREAM = 0x1;
constexpr uint8_t PROXY_PROTO_V2_TRANSPORT_DGRAM = 0x2;

constexpr uint32_t PROXY_PROTO_V2_ADDR_LEN_UNSPEC = 0;
constexpr uint32_t PROXY_PROTO_V2_ADDR_LEN_INET = 12;
constexpr uint32_t PROXY_PROTO_V2_ADDR_LEN_INET6 = 36;
constexpr uint32_t PROXY_PROTO_V2_ADDR_LEN_UNIX = 216;

// Generates the v1 PROXY protocol header and adds it to the specified buffer
void generateV1Header(const std::string& src_addr, const std::string& dst_addr, uint32_t src_port,
                      uint32_t dst_port, Network::Address::IpVersion ip_version,
                      Buffer::Instance& out);
void generateV1Header(const Network::Address::Ip& source_address,
                      const Network::Address::Ip& dest_address, Buffer::Instance& out);

// Generates the v2 PROXY protocol header and adds it to the specified buffer
// TCP is assumed as the transport protocol
void generateV2Header(const std::string& src_addr, const std::string& dst_addr, uint32_t src_port,
                      uint32_t dst_port, Network::Address::IpVersion ip_version,
                      Buffer::Instance& out);
void generateV2Header(const Network::Address::Ip& source_address,
                      const Network::Address::Ip& dest_address, Buffer::Instance& out);

// Generates the appropriate proxy proto header and appends it to the supplied buffer.
void generateProxyProtoHeader(const envoy::config::core::v3::ProxyProtocolConfig& config,
                              const Network::Connection& connection, Buffer::Instance& out);

// Generates the v2 PROXY protocol local command header and adds it to the specified buffer
void generateV2LocalHeader(Buffer::Instance& out);

} // namespace ProxyProtocol
} // namespace Common
} // namespace Extensions
} // namespace Envoy
