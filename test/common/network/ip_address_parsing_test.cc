#include <arpa/inet.h>

#include "source/common/network/ip_address_parsing.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {

TEST(IpAddressParsingTest, ParseIPv4Basic) {
  auto sa4_or = IpAddressParsing::parseIPv4("127.0.0.1", /*port=*/8080);
  ASSERT_TRUE(sa4_or.ok());
  const sockaddr_in sa4 = sa4_or.value();
  EXPECT_EQ(AF_INET, sa4.sin_family);
  EXPECT_EQ(htons(8080), sa4.sin_port);
  EXPECT_EQ(htonl(INADDR_LOOPBACK), sa4.sin_addr.s_addr);

  EXPECT_FALSE(IpAddressParsing::parseIPv4("1.2.3", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("1.2.3.256", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv4("not_an_ip", 0).ok());
}

TEST(IpAddressParsingTest, ParseIPv6Basic) {
  auto sa6_or = IpAddressParsing::parseIPv6("::1", /*port=*/443);
  ASSERT_TRUE(sa6_or.ok());
  const sockaddr_in6 sa6 = sa6_or.value();
  EXPECT_EQ(AF_INET6, sa6.sin6_family);
  EXPECT_EQ(htons(443), sa6.sin6_port);
  in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
  EXPECT_EQ(0, memcmp(&loopback, &sa6.sin6_addr, sizeof(in6_addr)));

  EXPECT_FALSE(IpAddressParsing::parseIPv6("::g", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv6("1:::1", 0).ok());
  EXPECT_FALSE(IpAddressParsing::parseIPv6("not_an_ip", 0).ok());
}

} // namespace Network
} // namespace Envoy
