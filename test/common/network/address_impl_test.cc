#include "envoy/common/exception.h"

#include "common/network/address_impl.h"

#include <sys/un.h>

namespace Network {
namespace Address {

TEST(Ipv4InstanceTest, SocketAddress) {
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);

  Ipv4Instance address(&addr4);
  EXPECT_EQ("1.2.3.4:6502", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("1.2.3.4", address.ip()->addressAsString());
  EXPECT_EQ(6502U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
}

TEST(Ipv4InstanceTest, AddressOnly) {
  Ipv4Instance address("3.4.5.6");
  EXPECT_EQ("3.4.5.6:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("3.4.5.6", address.ip()->addressAsString());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
}

TEST(Ipv4InstanceTest, AddressAndPort) {
  Ipv4Instance address("127.0.0.1", 80);
  EXPECT_EQ("127.0.0.1:80", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("127.0.0.1", address.ip()->addressAsString());
  EXPECT_FALSE(address.ip()->isAnyAddress());
  EXPECT_EQ(80U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
}

TEST(Ipv4InstanceTest, PortOnly) {
  Ipv4Instance address(443);
  EXPECT_EQ("0.0.0.0:443", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("0.0.0.0", address.ip()->addressAsString());
  EXPECT_TRUE(address.ip()->isAnyAddress());
  EXPECT_EQ(443U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
}

TEST(Ipv4InstanceTest, BadAddress) {
  EXPECT_THROW(Ipv4Instance("foo"), EnvoyException);
  EXPECT_THROW(Ipv4Instance("bar", 1), EnvoyException);
}

TEST(Ipv6InstanceTest, SocketAddress) {
  sockaddr_in6 addr6;
  addr6.sin6_family = AF_INET6;
  EXPECT_EQ(1, inet_pton(AF_INET6, "01:023::00Ef", &addr6.sin6_addr));
  addr6.sin6_port = htons(32000);

  Ipv6Instance address(addr6);
  EXPECT_EQ("[1:23::ef]:32000", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("1:23::ef", address.ip()->addressAsString());
  EXPECT_FALSE(address.ip()->isAnyAddress());
  EXPECT_EQ(32000U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
}

TEST(Ipv6InstanceTest, AddressOnly) {
  Ipv6Instance address("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  EXPECT_EQ("[2001:db8:85a3::8a2e:370:7334]:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("2001:db8:85a3::8a2e:370:7334", address.ip()->addressAsString());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
}

TEST(Ipv6InstanceTest, AddressAndPort) {
  Ipv6Instance address("::0001", 80);
  EXPECT_EQ("[::1]:80", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("::1", address.ip()->addressAsString());
  EXPECT_EQ(80U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
}

TEST(Ipv6InstanceTest, PortOnly) {
  Ipv6Instance address(443);
  EXPECT_EQ("[::]:443", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("::", address.ip()->addressAsString());
  EXPECT_TRUE(address.ip()->isAnyAddress());
  EXPECT_EQ(443U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
}

TEST(Ipv6InstanceTest, BadAddress) {
  EXPECT_THROW(Ipv6Instance("foo"), EnvoyException);
  EXPECT_THROW(Ipv6Instance("bar", 1), EnvoyException);
}

TEST(PipeInstanceTest, Basic) {
  PipeInstance address("/foo");
  EXPECT_EQ("/foo", address.asString());
  EXPECT_EQ(Type::Pipe, address.type());
  EXPECT_EQ(nullptr, address.ip());
}

} // Address
} // Network
