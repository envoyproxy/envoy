#include "source/common/header_routing/header_parser.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace HeaderRouting {
namespace {

// 构造 8 字节头部：Magic/Version 单字节 + RoomIP 4 字节大端 + RoomPort 2 字节大端。
std::string makeHeader(uint8_t magic, uint8_t version, uint32_t ip, uint16_t port) {
  std::string header(HeaderLength, '\0');
  header[MagicOffset] = static_cast<char>(magic);
  header[VersionOffset] = static_cast<char>(version);
  header[IpOffset] = static_cast<char>((ip >> 24) & 0xFF);
  header[IpOffset + 1] = static_cast<char>((ip >> 16) & 0xFF);
  header[IpOffset + 2] = static_cast<char>((ip >> 8) & 0xFF);
  header[IpOffset + 3] = static_cast<char>(ip & 0xFF);
  header[PortOffset] = static_cast<char>((port >> 8) & 0xFF);
  header[PortOffset + 1] = static_cast<char>(port & 0xFF);
  return header;
}

class HeaderParserTest : public testing::Test {
protected:
  // 默认配置：magic=0x55, version=1。
  HeaderRoutingConfig config_;
};

// 正常头部解析：10.0.0.3:8600。
TEST_F(HeaderParserTest, ParsesValidHeader) {
  ParseResult result = HeaderParser::parse(makeHeader(0x55, 1, 0x0A000003, 8600), config_);
  EXPECT_EQ(ParseResult::Status::Ok, result.status);
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ("10.0.0.3", result.target->ip);
  EXPECT_EQ(8600, result.target->port);
}

// 头部 + 尾部多余数据：解析成功，多余数据由调用方透传。
TEST_F(HeaderParserTest, IgnoresTrailingData) {
  // 192.168.1.254:65535，验证高位字节（> 127）无符号读取不误判。
  ParseResult result =
      HeaderParser::parse(makeHeader(0x55, 1, 0xC0A801FE, 65535) + "game payload", config_);
  EXPECT_EQ(ParseResult::Status::Ok, result.status);
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ("192.168.1.254", result.target->ip);
  EXPECT_EQ(65535, result.target->port);
}

// 长度恰为 8 字节（边界）：可解析。
TEST_F(HeaderParserTest, ParsesExactLengthHeader) {
  ParseResult result = HeaderParser::parse(makeHeader(0x55, 1, 0x7F000001, 1), config_);
  EXPECT_EQ(ParseResult::Status::Ok, result.status);
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ("127.0.0.1", result.target->ip);
  EXPECT_EQ(1, result.target->port);
}

// 长度不足头部（0~7 字节）→ NeedMoreData，target 无效。
TEST_F(HeaderParserTest, ReturnsNeedMoreDataForShortData) {
  for (size_t len = 0; len < HeaderLength; ++len) {
    const std::string input(len, 'a');
    ParseResult result = HeaderParser::parse(absl::string_view(input), config_);
    EXPECT_EQ(ParseResult::Status::NeedMoreData, result.status);
    EXPECT_FALSE(result.target.has_value());
  }
}

// Magic 不匹配 → BadMagic。
TEST_F(HeaderParserTest, RejectsBadMagic) {
  ParseResult result = HeaderParser::parse(makeHeader(0x54, 1, 0x0A000003, 8600), config_);
  EXPECT_EQ(ParseResult::Status::BadMagic, result.status);
  EXPECT_FALSE(result.target.has_value());
}

// Version 不匹配 → BadVersion。
TEST_F(HeaderParserTest, RejectsBadVersion) {
  ParseResult result = HeaderParser::parse(makeHeader(0x55, 2, 0x0A000003, 8600), config_);
  EXPECT_EQ(ParseResult::Status::BadVersion, result.status);
  EXPECT_FALSE(result.target.has_value());
}

// 自定义 magic/version 配置生效。
TEST_F(HeaderParserTest, UsesCustomMagicAndVersion) {
  HeaderRoutingConfig custom;
  custom.magic = 0xAA;
  custom.version = 3;
  ParseResult result = HeaderParser::parse(makeHeader(0xAA, 3, 0x0A000004, 9000), custom);
  EXPECT_EQ(ParseResult::Status::Ok, result.status);
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ("10.0.0.4", result.target->ip);
  EXPECT_EQ(9000, result.target->port);
}

} // namespace
} // namespace HeaderRouting
} // namespace Envoy
