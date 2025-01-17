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

  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, true, {}));

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

  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, false, {}));

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

  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, false, {0x5}));

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
  ASSERT_TRUE(generateV2Header(proxy_proto_data, buff, true, {}));

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

  EXPECT_LOG_CONTAINS("warn", "Generating Proxy Protocol V2 header: TLVs exceed length limit 65535",
                      generateV2Header(proxy_proto_data, buff, true, {}));
}

} // namespace
} // namespace ProxyProtocol
} // namespace Common
} // namespace Extensions
} // namespace Envoy
