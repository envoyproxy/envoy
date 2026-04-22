#include <arpa/inet.h>

#include "source/common/network/ip_address_parsing.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {

TEST(IpAddressParsingTest, ParseIPv4Valid) {
  // Test standard dotted-quad notation.
  auto sa4_or = IpAddressParsing::parseIPv4("127.0.0.1", /*port=*/8080);
  ASSERT_TRUE(sa4_or.ok());
  const sockaddr_in sa4 = sa4_or.value();
  EXPECT_EQ(AF_INET, sa4.sin_family);
  EXPECT_EQ(htons(8080), sa4.sin_port);
  EXPECT_EQ(htonl(INADDR_LOOPBACK), sa4.sin_addr.s_addr);

  // Test another valid IPv4.
  auto sa4_or2 = IpAddressParsing::parseIPv4("192.168.1.1", /*port=*/443);
  ASSERT_TRUE(sa4_or2.ok());
  const sockaddr_in sa4_2 = sa4_or2.value();
  EXPECT_EQ(AF_INET, sa4_2.sin_family);
  EXPECT_EQ(htons(443), sa4_2.sin_port);
  char buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &sa4_2.sin_addr, buf, INET_ADDRSTRLEN);
  EXPECT_EQ("192.168.1.1", std::string(buf));

  // Test edge case IPs.
  EXPECT_TRUE(IpAddressParsing::parseIPv4("0.0.0.0", 0).ok());
  EXPECT_TRUE(IpAddressParsing::parseIPv4("255.255.255.255", 65535).ok());
}

TEST(IpAddressParsingTest, ParseIPv4Invalid) {
  // Test incomplete addresses.
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1.2.3", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1.2", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1", 0).ok());

  // Test out-of-range octets.
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1.2.3.256", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("256.0.0.1", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1.2.3.999", 0).ok());

  // Test non-numeric input.
  EXPECT_FALSE(IpAddressParsing::parseIPv4("not_an_ip", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("abc.def.ghi.jkl", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("", 0).ok());

  // Test IPv6 addresses (should fail for IPv4 parser).
  EXPECT_FALSE(IpAddressParsing::parseIPv4("::1", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("fe80::1", 0).ok());

  // Test that inet_pton() correctly rejects non-standard formats.
  // These formats might be accepted by some liberal parsers but should be rejected
  // by inet_pton() which enforces strict dotted-quad notation.
  EXPECT_FALSE(IpAddressParsing::parseIPv4("127.1", 0).ok());      // Short form
  EXPECT_FALSE(IpAddressParsing::parseIPv4("0x7f.0.0.1", 0).ok()); // Hex notation
  // Note: Some platforms (e.g., macOS) may accept octal notation in inet_pton(),
  // so we skip this test to maintain platform compatibility.

  // Test extra dots.
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1.2.3.4.", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4(".1.2.3.4", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1..2.3.4", 0).ok());

  // Test with spaces.
  EXPECT_FALSE(IpAddressParsing::parseIPv4(" 1.2.3.4", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1.2.3.4 ", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1.2. 3.4", 0).ok());
}

TEST(IpAddressParsingTest, ParseIPv6Valid) {
  // Test loopback.
  auto sa6_or = IpAddressParsing::parseIPv6("::1", /*port=*/443);
  ASSERT_TRUE(sa6_or.ok());
  const sockaddr_in6 sa6 = sa6_or.value();
  EXPECT_EQ(AF_INET6, sa6.sin6_family);
  EXPECT_EQ(htons(443), sa6.sin6_port);
  in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
  EXPECT_EQ(0, memcmp(&loopback, &sa6.sin6_addr, sizeof(in6_addr)));

  // Test other valid IPv6 addresses.
  EXPECT_TRUE(IpAddressParsing::parseIPv6("::", 0).ok());
  EXPECT_TRUE(IpAddressParsing::parseIPv6("::ffff:127.0.0.1", 0).ok()); // IPv4-mapped
  EXPECT_TRUE(IpAddressParsing::parseIPv6("2001:db8::1", 0).ok());
  EXPECT_TRUE(IpAddressParsing::parseIPv6("fe80::1", 0).ok());
  EXPECT_TRUE(IpAddressParsing::parseIPv6("2001:0db8:0000:0000:0000:0000:0000:0001", 0).ok());

  // Test IPv6 with scope (this is why we need getaddrinfo() for IPv6).
  // Note: Actual scope parsing depends on platform support.
#ifdef __linux__
  // Numeric scope ID.
  auto sa6_scope = IpAddressParsing::parseIPv6("fe80::1%2", 80);
  ASSERT_TRUE(sa6_scope.ok());
  EXPECT_EQ(2, sa6_scope.value().sin6_scope_id);
#endif
}

TEST(IpAddressParsingTest, ParseIPv6Invalid) {
  // Test invalid characters.
  EXPECT_FALSE(IpAddressParsing::parseIPv6("::g", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv6("gggg::1", 0).ok());

  // Test invalid format.
  EXPECT_FALSE(IpAddressParsing::parseIPv6("1:::1", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv6(":::1", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv6("1::2::3", 0).ok()); // Multiple ::

  // Test non-IP strings.
  EXPECT_FALSE(IpAddressParsing::parseIPv6("not_an_ip", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv6("", 0).ok());

  // Test IPv4 addresses (should fail for IPv6 parser).
  EXPECT_FALSE(IpAddressParsing::parseIPv6("127.0.0.1", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv6("192.168.1.1", 0).ok());

  // Test too many groups.
  EXPECT_FALSE(IpAddressParsing::parseIPv6("1:2:3:4:5:6:7:8:9", 0).ok());

  // Test with spaces.
  EXPECT_FALSE(IpAddressParsing::parseIPv6(" ::1", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv6("::1 ", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv6(":: 1", 0).ok());
}

TEST(IpAddressParsingTest, PortBoundaries) {
  // Test port boundaries for IPv4.
  EXPECT_TRUE(IpAddressParsing::parseIPv4("127.0.0.1", 0).ok());
  EXPECT_TRUE(IpAddressParsing::parseIPv4("127.0.0.1", 65535).ok());

  // Test port boundaries for IPv6.
  EXPECT_TRUE(IpAddressParsing::parseIPv6("::1", 0).ok());
  EXPECT_TRUE(IpAddressParsing::parseIPv6("::1", 65535).ok());
}

} // namespace Network
} // namespace Envoy
