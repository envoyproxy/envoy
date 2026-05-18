#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

#include "test/mocks/network/connection.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ProxyProtocol {
namespace {

using namespace std::literals::string_literals;

TEST(ProxyProtocolHeaderTest, GeneratesV1IPv4Header) {
  const auto expectedHeaderStr = "PROXY TCP4 174.2.2.222 172.0.0.1 50000 80\r\n";
  const Buffer::OwnedImpl expectedBuff(expectedHeaderStr);
  const auto src_addr = "174.2.2.222";
  const auto dst_addr = "172.0.0.1";
  const auto src_port = 50000;
  const auto dst_port = 80;
  const auto version = Network::Address::IpVersion::v4;
  Buffer::OwnedImpl buff{};

  generateV1Header(src_addr, dst_addr, src_port, dst_port, version, buff);

  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));

  // Make sure the wrapper utility generates the same output.
  testing::NiceMock<Network::MockClientConnection> connection;
  connection.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      *Network::Utility::resolveUrl("tcp://174.2.2.222:50000"));
  connection.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      *Network::Utility::resolveUrl("tcp://172.0.0.1:80"));
  Buffer::OwnedImpl util_buf;
  envoy::config::core::v3::ProxyProtocolConfig config;
  config.set_version(envoy::config::core::v3::ProxyProtocolConfig::V1);
  generateProxyProtoHeader(config, connection, util_buf);
  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, util_buf));
}

TEST(ProxyProtocolHeaderTest, GeneratesV1IPv6Header) {
  const auto expectedHeaderStr = "PROXY TCP6 1::2:3 a:b:c:d:: 50000 80\r\n";
  const Buffer::OwnedImpl expectedBuff(expectedHeaderStr);
  const auto src_addr = "1::2:3";
  const auto dst_addr = "a:b:c:d::";
  const auto src_port = 50000;
  const auto dst_port = 80;
  const auto version = Network::Address::IpVersion::v6;
  Buffer::OwnedImpl buff{};

  generateV1Header(src_addr, dst_addr, src_port, dst_port, version, buff);

  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2IPv4Header) {
  const uint8_t v2_protocol[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                 0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x02, 0x01};
  const Buffer::OwnedImpl expectedBuff(v2_protocol, sizeof(v2_protocol));
  const auto src_addr = "1.2.3.4";
  const auto dst_addr = "0.1.1.2";
  const auto src_port = 773;
  const auto dst_port = 513;
  const auto version = Network::Address::IpVersion::v4;
  Buffer::OwnedImpl buff{};

  generateV2Header(src_addr, dst_addr, src_port, dst_port, version, buff);

  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2IPv6Header) {
  const uint8_t v2_protocol[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                 0x0a, 0x21, 0x21, 0x00, 0x24, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
                                 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02};
  const Buffer::OwnedImpl expectedBuff(v2_protocol, sizeof(v2_protocol));
  const auto src_addr = "1:2:3::4";
  const auto dst_addr = "1:100:200:3::";
  const auto src_port = 8;
  const auto dst_port = 2;
  const auto version = Network::Address::IpVersion::v6;
  Buffer::OwnedImpl buff{};

  generateV2Header(src_addr, dst_addr, src_port, dst_port, version, buff);

  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));

  // Make sure the wrapper utility generates the same output.
  testing::NiceMock<Network::MockConnection> connection;
  connection.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      *Network::Utility::resolveUrl("tcp://[1:2:3::4]:8"));
  connection.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      *Network::Utility::resolveUrl("tcp://[1:100:200:3::]:2"));
  Buffer::OwnedImpl util_buf;
  envoy::config::core::v3::ProxyProtocolConfig config;
  config.set_version(envoy::config::core::v3::ProxyProtocolConfig::V2);
  generateProxyProtoHeader(config, connection, util_buf);
  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, util_buf));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2LocalHeader) {
  const uint8_t v2_protocol[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51,
                                 0x55, 0x49, 0x54, 0x0a, 0x20, 0x00, 0x00, 0x00};
  const Buffer::OwnedImpl expectedBuff(v2_protocol, sizeof(v2_protocol));
  Buffer::OwnedImpl buff{};

  generateV2LocalHeader(buff);

  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2IPv4HeaderWithTLVPassAll) {
  const uint8_t v2_protocol[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                 0x0a, 0x21, 0x11, 0x00, 0x11, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01,
                                 0x01, 0x02, 0x03, 0x05, 0x02, 0x01, 0x05, 0x00, 0x02, 0x06, 0x07};

  const Buffer::OwnedImpl expectedBuff(v2_protocol, sizeof(v2_protocol));
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  Network::ProxyProtocolTLV tlv{0x5, {0x06, 0x07}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {tlv}};
  Buffer::OwnedImpl buff{};
  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, true, {}, {}));

  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2IPv4HeaderWithTLVPassEmpty) {
  const uint8_t v2_protocol[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                 0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x02, 0x01};

  const Buffer::OwnedImpl expectedBuff(v2_protocol, sizeof(v2_protocol));
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  Network::ProxyProtocolTLV tlv{0x5, {0x06, 0x07}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {tlv}};
  Buffer::OwnedImpl buff{};

  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, false, {}, {}));

  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2IPv4HeaderWithTLVPassSpecific) {
  const uint8_t v2_protocol[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                 0x0a, 0x21, 0x11, 0x00, 0x11, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01,
                                 0x01, 0x02, 0x03, 0x05, 0x02, 0x01, 0x05, 0x00, 0x02, 0x06, 0x07};

  const Buffer::OwnedImpl expectedBuff(v2_protocol, sizeof(v2_protocol));
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  Network::ProxyProtocolTLV tlv{0x5, {0x06, 0x07}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {tlv}};
  Buffer::OwnedImpl buff{};

  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, false, {0x5}, {}));

  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2IPv6HeaderWithTLV) {
  const uint8_t v2_protocol[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21, 0x21, 0x00,
      0x29, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x04, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x05, 0x00, 0x02, 0x06, 0x07};
  const Buffer::OwnedImpl expectedBuff(v2_protocol, sizeof(v2_protocol));
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv6Instance("1:2:3::4", 8));
  auto dst_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv6Instance("1:100:200:3::", 2));
  Network::ProxyProtocolTLV tlv{0x5, {0x06, 0x07}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {tlv}};

  Buffer::OwnedImpl buff{};
  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, true, {}, {}));

  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2WithTLVExceedingLengthLimit) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  const std::string long_tlv(65536, 'a');
  Network::ProxyProtocolTLV tlv{0x5, std::vector<unsigned char>(long_tlv.begin(), long_tlv.end())};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {tlv}};
  Buffer::OwnedImpl buff{};

  EXPECT_LOG_CONTAINS("warn", "Skipping TLV type 5 because adding it would exceed the 65535 limit",
                      generateV2Header(proxy_proto_data, buff, true, {}, {}));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2WithCustomTLVs) {
  const uint8_t v2_protocol[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21,
      0x11, 0x00, 0x15, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01, 0x01, 0x02, 0x03, 0x05,
      0x02, 0x01, 0x08, 0x00, 0x01, 0x08, 0xD3, 0x00, 0x02, 0x06, 0x07,
  };

  const Buffer::OwnedImpl expectedBuff(v2_protocol, sizeof(v2_protocol));
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  Network::ProxyProtocolTLV tlv{0xD3, {0x06, 0x07}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {tlv}};
  std::vector<Envoy::Network::ProxyProtocolTLV> custom_tlvs = {
      {0x8, {0x08}},
  };
  Buffer::OwnedImpl buff{};

  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, false, {0xD3}, custom_tlvs));
  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2WithCustomTLVExceedingLengthLimit) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  Network::ProxyProtocolTLV tlv{0x5, {0x06, 0x07}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {tlv}};
  Buffer::OwnedImpl buff{};
  std::vector<Envoy::Network::ProxyProtocolTLV> custom_tlvs = {
      {0x8, std::vector<unsigned char>(65536, 'a')},
  };
  EXPECT_LOG_CONTAINS("warn", "Skipping TLV type 8 because adding it would exceed the 65535 limit",
                      generateV2Header(proxy_proto_data, buff, true, {}, custom_tlvs));
}

TEST(ProxyProtocolHeaderTest, GeneratesV2WithCustomTLVsNoPassthrough) {
  const uint8_t v2_protocol[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
      0x0a, 0x21, 0x11, 0x00, 0x10, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01,
      0x01, 0x02, 0x03, 0x05, 0x02, 0x01, 0xD3, 0x00, 0x01, 0x09,
  };

  const Buffer::OwnedImpl expectedBuff(v2_protocol, sizeof(v2_protocol));
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  Network::ProxyProtocolTLV tlv{0xD5, {0x06, 0x07}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {tlv}};
  std::vector<Envoy::Network::ProxyProtocolTLV> custom_tlvs = {
      {0xD3, {0x09}},
  };
  Buffer::OwnedImpl buff{};

  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, false, {}, custom_tlvs));
  EXPECT_TRUE(TestUtility::buffersEqual(expectedBuff, buff));
}

// Validate that any combined TLVs over the maximum length are removed.
TEST(ProxyProtocolHeaderTest, SkippedTLVs) {
  auto src_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv4Instance("1.2.3.4", 12345));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("5.6.7.8", 443));

  // Attacker passthrough TLV: 65520-byte value containing a smuggled HTTP
  // request after the 3-byte TLV header. This is the value that "spills over"
  // when the TLV is "skipped" but emitted anyway.
  std::string smuggled = "\r\nGET /admin HTTP/1.1\r\nHost: internal\r\n\r\n";
  std::vector<unsigned char> attack_value(smuggled.begin(), smuggled.end());
  attack_value.resize(65520, '#'); // pad with #
  Network::ProxyProtocolTLV attacker_tlv{0x0a, attack_value};

  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {attacker_tlv}};

  // Config: 2 small custom `TLVs` (canonical AWS pattern: `VPC-ID` + region).
  // Their presence is what makes combined_tlv_vector != just-the-passthrough,
  // pushing the total over 65535 so the attacker TLV gets "skipped".
  std::vector<Envoy::Network::ProxyProtocolTLV> custom_tlvs = {
      {0xea, std::vector<unsigned char>(10, 0x41)}, // 13 bytes total
      {0xeb, std::vector<unsigned char>(10, 0x42)}, // 13 bytes total
  };

  Buffer::OwnedImpl buff{};
  // Passthrough all `TLVs` so the attacker TLV is in `proxy_proto_data.tlv_vector_`
  // and gets considered.
  const bool pass_all_tlvs = true;
  EXPECT_LOG_CONTAINS("warn", "Skipping TLV type 10 because adding it would exceed the 65535 limit",
                      generateV2Header(proxy_proto_data, buff, pass_all_tlvs, {}, custom_tlvs));

  // Proxy protocol v2 wire format: [12B signature][1B version+command][1B family+protocol][2B
  // length][len bytes]
  //   = 16-byte fixed prefix + len bytes
  // The 2-byte length at offset 14-15 (big-endian) advertises everything after.
  ASSERT_GE(buff.length(), 16u);
  uint8_t hdr[16];
  buff.copyOut(0, 16, hdr);
  uint16_t advertised_len = (static_cast<uint16_t>(hdr[14]) << 8) | hdr[15];

  uint64_t actual_body_len = buff.length() - 16;

  // advertised_len = 12 (INET addresses) + 13 (custom 0xea) + 13 (custom 0xeb) = 38
  // actual_body_len = 38 + 3 (TLV 0x0a header) + 65520 (TLV 0x0a value) = 65561
  // The 65523-byte difference is the attacker spillover.
  EXPECT_EQ(actual_body_len, advertised_len);
}

// Validate that a TLV which is small enough to satisfy a naive 65535-byte cap on the
// TLV portion alone (extension_length <= 65535) but which would push the on-wire `len`
// field (addr struct + extensions) past 65535 is correctly skipped, and the resulting
// header's advertised len matches the actual body length.
TEST(ProxyProtocolHeaderTest, SkipsTLVThatWouldOverflowAddrPlusExtensionLength) {
  auto src_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv4Instance("1.2.3.4", 12345));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("5.6.7.8", 443));

  // value.size() = 65530 → TLV (3-byte header + value) = 65533 bytes.
  // Old check: 65533 <= 65535, allowed. extension_length becomes 65533.
  // Then addr_length = 12 (INET) + 65533 = 65545, which truncates to 9 in uint16_t — the
  // wire smuggling bug. New check: 65533 > (65535 - 12), skip.
  std::vector<unsigned char> overflow_value(65530, 'X');
  Network::ProxyProtocolTLV overflow_tlv{0xAA, overflow_value};

  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {overflow_tlv}};

  Buffer::OwnedImpl buff{};
  const bool pass_all_tlvs = true;
  EXPECT_LOG_CONTAINS("warn",
                      "Skipping TLV type 170 because adding it would exceed the 65535 limit",
                      generateV2Header(proxy_proto_data, buff, pass_all_tlvs, {}, {}));

  // The advertised wire `len` (offset 14-15, big-endian) must equal what we actually wrote
  // after the 16-byte fixed prefix.
  ASSERT_GE(buff.length(), 16u);
  uint8_t hdr[16];
  buff.copyOut(0, 16, hdr);
  uint16_t advertised_len = (static_cast<uint16_t>(hdr[14]) << 8) | hdr[15];
  uint64_t actual_body_len = buff.length() - 16;
  EXPECT_EQ(actual_body_len, advertised_len);

  // With the overflow TLV skipped and no other TLVs, only the 12-byte IPv4 address
  // struct should be present.
  EXPECT_EQ(advertised_len, 12u);
}

// Validate the IPv6 boundary: with addr struct = 36, an extension of 65500 bytes (= 3 + 65497
// from a single TLV) would be allowed by the old 65535 cap (65500 <= 65535) but would push
// addr_length to 36 + 65500 = 65536, overflowing.
TEST(ProxyProtocolHeaderTest, SkipsTLVThatWouldOverflowAddrPlusExtensionLengthIPv6) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv6Instance("::1", 12345));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv6Instance("::2", 443));

  std::vector<unsigned char> overflow_value(65497, 'Y');
  Network::ProxyProtocolTLV overflow_tlv{0xBB, overflow_value};

  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {overflow_tlv}};

  Buffer::OwnedImpl buff{};
  const bool pass_all_tlvs = true;
  EXPECT_LOG_CONTAINS("warn",
                      "Skipping TLV type 187 because adding it would exceed the 65535 limit",
                      generateV2Header(proxy_proto_data, buff, pass_all_tlvs, {}, {}));

  ASSERT_GE(buff.length(), 16u);
  uint8_t hdr[16];
  buff.copyOut(0, 16, hdr);
  uint16_t advertised_len = (static_cast<uint16_t>(hdr[14]) << 8) | hdr[15];
  uint64_t actual_body_len = buff.length() - 16;
  EXPECT_EQ(actual_body_len, advertised_len);
  EXPECT_EQ(advertised_len, 36u);
}

// Validate the boundary right at the limit: with addr struct = 12 and a TLV producing
// extension_length = 65523 (= 3 + 65520), the wire len is exactly 65535 — the largest
// legal value — and the TLV is kept.
TEST(ProxyProtocolHeaderTest, KeepsTLVAtExactBoundary) {
  auto src_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv4Instance("1.2.3.4", 12345));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("5.6.7.8", 443));

  std::vector<unsigned char> max_value(65520, 'Z');
  Network::ProxyProtocolTLV max_tlv{0xCC, max_value};

  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {max_tlv}};

  Buffer::OwnedImpl buff{};
  const bool pass_all_tlvs = true;
  EXPECT_TRUE(generateV2Header(proxy_proto_data, buff, pass_all_tlvs, {}, {}));

  ASSERT_GE(buff.length(), 16u);
  uint8_t hdr[16];
  buff.copyOut(0, 16, hdr);
  uint16_t advertised_len = (static_cast<uint16_t>(hdr[14]) << 8) | hdr[15];
  uint64_t actual_body_len = buff.length() - 16;
  EXPECT_EQ(advertised_len, std::numeric_limits<uint16_t>::max());
  EXPECT_EQ(actual_body_len, advertised_len);
}

// Defensive check on the low-level overload: passing an extension_length that, combined with the
// address struct, would overflow the 16 bit length field triggers ENVOY_BUG. Instead of incorrectly
// framing on the wire, the function emits a 16-byte v2-shaped header with a deliberately invalid
// command and length=0 so the peer drops per spec while the framing stays self-consistent.
TEST(ProxyProtocolHeaderTest, LowLevelGenerateV2HeaderRejectsOverflow) {
  Buffer::OwnedImpl buff;
  EXPECT_ENVOY_BUG(
      {
        generateV2Header("1.2.3.4", "5.6.7.8", 12345, 443, Network::Address::IpVersion::v4,
                         /*extension_length=*/65530, buff);
        ASSERT_EQ(buff.length(), 16u);
        uint8_t hdr[16];
        buff.copyOut(0, 16, hdr);
        // Bytes 0-11: v2 signature so the peer recognizes this as proxy protocol v2.
        EXPECT_EQ(0, ::memcmp(hdr, PROXY_PROTO_V2_SIGNATURE, PROXY_PROTO_V2_SIGNATURE_LEN));
        // Byte 12: version=2 in the upper nibble, command=0xF (unassigned) in the lower nibble.
        // Per proxy-protocol.txt the receiver must drop on unexpected command values.
        EXPECT_EQ(hdr[12], 0x2F);
        // Bytes 14-15: length=0 so the on-wire framing is self-consistent (no trailing bytes).
        const uint16_t advertised_len = (static_cast<uint16_t>(hdr[14]) << 8) | hdr[15];
        EXPECT_EQ(advertised_len, 0u);
        EXPECT_EQ(buff.length() - 16, advertised_len);
      },
      "v2 PROXY protocol header length would overflow uint16_t");
}

} // namespace
} // namespace ProxyProtocol
} // namespace Common
} // namespace Extensions
} // namespace Envoy
