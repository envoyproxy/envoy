#include "envoy/common/exception.h"

#include "common/network/address_impl.h"

#include <sys/un.h>

// We are adding things into the std namespace.
// Note that this is technically undefined behavior!
namespace std {

// Pair
template <typename First, typename Second>
std::ostream& operator<<(std::ostream& out, const std::pair<First, Second>& p) {
  return out << '(' << p.first << ", " << p.second << ')';
}

} // std

namespace Network {
namespace Address {
namespace {

bool addressesEqual(const InstancePtr& a, const Instance& b) {
  if (a == nullptr || a->type() != Type::Ip || b.type() != Type::Ip) {
    return false;
  } else {
    return a->ip()->addressAsString() == b.ip()->addressAsString();
  }
}

} // namespace

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
  EXPECT_TRUE(addressesEqual(parseInternetAddress("1.2.3.4"), address));
}

TEST(Ipv4InstanceTest, AddressOnly) {
  Ipv4Instance address("3.4.5.6");
  EXPECT_EQ("3.4.5.6:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("3.4.5.6", address.ip()->addressAsString());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
  EXPECT_TRUE(addressesEqual(parseInternetAddress("3.4.5.6"), address));
}

TEST(Ipv4InstanceTest, AddressAndPort) {
  Ipv4Instance address("127.0.0.1", 80);
  EXPECT_EQ("127.0.0.1:80", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("127.0.0.1", address.ip()->addressAsString());
  EXPECT_EQ(80U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
  EXPECT_TRUE(addressesEqual(parseInternetAddress("127.0.0.1"), address));
}

TEST(Ipv4InstanceTest, PortOnly) {
  Ipv4Instance address(443);
  EXPECT_EQ("0.0.0.0:443", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("0.0.0.0", address.ip()->addressAsString());
  EXPECT_EQ(443U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
  EXPECT_TRUE(addressesEqual(parseInternetAddress("0.0.0.0"), address));
}

TEST(Ipv4InstanceTest, BadAddress) {
  EXPECT_THROW(Ipv4Instance("foo"), EnvoyException);
  EXPECT_THROW(Ipv4Instance("bar", 1), EnvoyException);
  EXPECT_EQ(parseInternetAddress(""), nullptr);
  EXPECT_EQ(parseInternetAddress("1.2.3"), nullptr);
  EXPECT_EQ(parseInternetAddress("1.2.3.4.5"), nullptr);
  EXPECT_EQ(parseInternetAddress("1.2.3.256"), nullptr);
  EXPECT_EQ(parseInternetAddress("foo"), nullptr);
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
  EXPECT_EQ(32000U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(parseInternetAddress("1:0023::0Ef"), address));
}

TEST(Ipv6InstanceTest, AddressOnly) {
  Ipv6Instance address("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  EXPECT_EQ("[2001:db8:85a3::8a2e:370:7334]:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("2001:db8:85a3::8a2e:370:7334", address.ip()->addressAsString());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(parseInternetAddress("2001:db8:85a3::8a2e:0370:7334"), address));
}

TEST(Ipv6InstanceTest, AddressAndPort) {
  Ipv6Instance address("::0001", 80);
  EXPECT_EQ("[::1]:80", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("::1", address.ip()->addressAsString());
  EXPECT_EQ(80U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(parseInternetAddress("0:0:0:0:0:0:0:1"), address));
}

TEST(Ipv6InstanceTest, PortOnly) {
  Ipv6Instance address(443);
  EXPECT_EQ("[::]:443", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("::", address.ip()->addressAsString());
  EXPECT_EQ(443U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(parseInternetAddress("::0000"), address));
}

TEST(Ipv6InstanceTest, BadAddress) {
  EXPECT_THROW(Ipv6Instance("foo"), EnvoyException);
  EXPECT_THROW(Ipv6Instance("bar", 1), EnvoyException);
  EXPECT_EQ(parseInternetAddress("0:0:0:0"), nullptr);
  EXPECT_EQ(parseInternetAddress("fffff::"), nullptr);
  EXPECT_EQ(parseInternetAddress("/foo"), nullptr);
}

TEST(PipeInstanceTest, Basic) {
  PipeInstance address("/foo");
  EXPECT_EQ("/foo", address.asString());
  EXPECT_EQ(Type::Pipe, address.type());
  EXPECT_EQ(nullptr, address.ip());
}

TEST(TruncateIpAddressAndLength, Various) {
  std::map<std::pair<std::string, int>, std::pair<std::string, int>> test_cases = {
      // IPv4
      {{"1.2.3.5", -100}, {"", -1}},
      {{"1.2.3.5", 0}, {"0.0.0.0", 0}},
      {{"1.2.3.5", 1}, {"0.0.0.0", 1}},
      {{"1.2.3.5", 7}, {"0.0.0.0", 7}},
      {{"1.2.3.5", 8}, {"1.0.0.0", 8}},
      {{"1.2.3.5", 14}, {"1.0.0.0", 14}},
      {{"1.2.3.5", 15}, {"1.2.0.0", 15}},
      {{"1.2.3.5", 22}, {"1.2.0.0", 22}},
      {{"1.2.3.5", 23}, {"1.2.2.0", 23}},
      {{"1.2.3.5", 24}, {"1.2.3.0", 24}},
      {{"1.2.3.5", 29}, {"1.2.3.0", 29}},
      {{"1.2.3.5", 30}, {"1.2.3.4", 30}},
      {{"1.2.3.5", 31}, {"1.2.3.4", 31}},
      {{"1.2.3.5", 32}, {"1.2.3.5", 32}},
      {{"1.2.3.5", 33}, {"1.2.3.5", 32}},
      // IPv6
      {{"::", -100}, {"", -1}},
      {{"ffff::ffff", 0}, {"::", 0}},
      {{"ffff::ffff", 1}, {"8000::", 1}},
      {{"ffff::ffff", 7}, {"fe00::", 7}},
      {{"ffff::ffff", 8}, {"ff00::", 8}},
      {{"ffff::ffff", 9}, {"ff80::", 9}},
      {{"ffff::ffff", 10}, {"ffc0::", 10}},
      {{"ffff::ffff", 15}, {"fffe::", 15}},
      {{"ffff::ffff", 16}, {"ffff::", 16}},
      {{"ffff::ffff", 17}, {"ffff::", 17}},
      {{"ffff::ffff", 112}, {"ffff::", 112}},
      {{"ffff::ffff", 113}, {"ffff::8000", 113}},
      {{"ffff::ffff", 119}, {"ffff::fe00", 119}},
      {{"ffff::ffff", 120}, {"ffff::ff00", 120}},
      {{"ffff::ffff", 121}, {"ffff::ff80", 121}},
      {{"ffff::ffff", 127}, {"ffff::fffe", 127}},
      {{"ffff::ffff", 128}, {"ffff::ffff", 128}},
      {{"ffff::ffff", 999}, {"ffff::ffff", 128}},
  };
  test_cases.size();
  for (const auto& kv : test_cases) {
    InstancePtr inPtr = parseInternetAddress(kv.first.first);
    EXPECT_TRUE(inPtr != nullptr) << kv.first.first;
    int length_io = kv.first.second;
    InstancePtr outPtr = truncateIpAddressAndLength(inPtr, &length_io);
    if (kv.second.second == -1) {
      EXPECT_EQ(outPtr, nullptr) << outPtr->asString() << "\n" << kv;
      EXPECT_EQ(length_io, -1) << kv;
    } else {
      ASSERT_TRUE(outPtr != nullptr) << kv;
      EXPECT_EQ(outPtr->ip()->addressAsString(), kv.second.first) << kv;
      EXPECT_EQ(length_io, kv.second.second) << kv;
    }
  }
} // TruncateIpAddressAndLength

TEST(Ipv4CidrRangeTest, InstancePtrAndLengthCtor) {
  InstancePtr ptr = parseInternetAddress("1.2.3.5");
  CidrRange rng(CidrRange::construct(ptr, 31)); // Copy ctor.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.length(), 31);
  EXPECT_EQ(rng.version(), IpVersion::v4);
  EXPECT_EQ(rng.asString(), "1.2.3.4/31");
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.3")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.4")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.5")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.6")));

  CidrRange rng2(CidrRange::construct(ptr, -1)); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  ptr.reset();
  CidrRange rng3(CidrRange::construct(ptr, 10)); // Invalid address.
  EXPECT_FALSE(rng3.isValid());
}

TEST(Ipv4CidrRangeTest, StringAndLengthCtor) {
  CidrRange rng;
  rng = CidrRange::construct("1.2.3.4", 31); // Assignment operator.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "1.2.3.4/31");
  EXPECT_EQ(rng.length(), 31);
  EXPECT_EQ(rng.version(), IpVersion::v4);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.3")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.4")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.5")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.6")));

  rng = CidrRange::construct("1.2.3.4", -10); // Invalid length.
  EXPECT_FALSE(rng.isValid());

  rng = CidrRange::construct("bogus", 31); // Invalid address.
  EXPECT_FALSE(rng.isValid());
}

TEST(Ipv4CidrRangeTest, StringCtor) {
  CidrRange rng = CidrRange::construct("1.2.3.4/31");
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "1.2.3.4/31");
  EXPECT_EQ(rng.length(), 31);
  EXPECT_EQ(rng.version(), IpVersion::v4);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.3")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.4")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.5")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.6")));

  CidrRange rng2 = CidrRange::construct("1.2.3.4/-10"); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  CidrRange rng3 = CidrRange::construct("bogus/31"); // Invalid address.
  EXPECT_FALSE(rng3.isValid());

  CidrRange rng4 = CidrRange::construct("/31"); // Missing address.
  EXPECT_FALSE(rng4.isValid());

  CidrRange rng5 = CidrRange::construct("1.2.3.4/"); // Missing length.
  EXPECT_FALSE(rng5.isValid());
}

TEST(Ipv4CidrRangeTest, BigRange) {
  CidrRange rng = CidrRange::construct("10.255.255.255/8");
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "10.0.0.0/8");
  EXPECT_EQ(rng.length(), 8);
  EXPECT_EQ(rng.version(), IpVersion::v4);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("9.255.255.255")));
  std::string addr;
  for (int i = 0; i < 256; ++i) {
    addr = fmt::format("10.{}.0.1", i);
    EXPECT_TRUE(rng.isInRange(parseInternetAddress(addr))) << addr;
    addr = fmt::format("10.{}.255.255", i);
    EXPECT_TRUE(rng.isInRange(parseInternetAddress(addr))) << addr;
  }
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("11.0.0.0")));
}

TEST(Ipv6CidrRange, InstancePtrAndLengthCtor) {
  InstancePtr ptr = parseInternetAddress("abcd::0345");
  CidrRange rng(CidrRange::construct(ptr, 127)); // Copy ctor.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.length(), 127);
  EXPECT_EQ(rng.version(), IpVersion::v6);
  EXPECT_EQ(rng.asString(), "abcd::344/127");
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("abcd::343")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("abcd::344")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("abcd::345")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("abcd::346")));

  CidrRange rng2(CidrRange::construct(ptr, -1)); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  ptr.reset();
  CidrRange rng3(CidrRange::construct(ptr, 127)); // Invalid address.
  EXPECT_FALSE(rng3.isValid());
}

TEST(Ipv6CidrRange, StringAndLengthCtor) {
  CidrRange rng;
  rng = CidrRange::construct("::ffff", 122); // Assignment operator.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "::ffc0/122");
  EXPECT_EQ(rng.length(), 122);
  EXPECT_EQ(rng.version(), IpVersion::v6);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("::ffbf")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("::ffc0")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("::ffff")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("::1:0")));

  rng = CidrRange::construct("::ffff", -2); // Invalid length.
  EXPECT_FALSE(rng.isValid());

  rng = CidrRange::construct("bogus", 122); // Invalid address.
  EXPECT_FALSE(rng.isValid());
}

TEST(Ipv6CidrRange, StringCtor) {
  CidrRange rng = CidrRange::construct("::fc1f/118");
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "::fc00/118");
  EXPECT_EQ(rng.length(), 118);
  EXPECT_EQ(rng.version(), IpVersion::v6);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("::fbff")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("::fc00")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("::ffff")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("::1:00")));

  CidrRange rng2 = CidrRange::construct("::fc1f/-10"); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  CidrRange rng3 = CidrRange::construct("::fc1f00/118"); // Invalid address.
  EXPECT_FALSE(rng3.isValid());

  CidrRange rng4 = CidrRange::construct("/118"); // Missing address.
  EXPECT_FALSE(rng4.isValid());

  CidrRange rng5 = CidrRange::construct("::fc1f/"); // Missing length.
  EXPECT_FALSE(rng5.isValid());
}

TEST(Ipv6CidrRange, BigRange) {
  std::string prefix = "2001:0db8:85a3:0000";
  CidrRange rng = CidrRange::construct(prefix + "::/64");
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "2001:db8:85a3::/64");
  EXPECT_EQ(rng.length(), 64);
  EXPECT_EQ(rng.version(), IpVersion::v6);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("2001:0db8:85a2:ffff:ffff:ffff:ffff:ffff")));
  std::string addr;
  for (char c : std::string("0123456789abcdef")) {
    addr = fmt::format("{}:000{}::", prefix, std::string(1, c));
    EXPECT_TRUE(rng.isInRange(parseInternetAddress(addr))) << addr << " not in " << rng.asString();
    addr = fmt::format("{}:fff{}:ffff:ffff:ffff", prefix, std::string(1, c));
    EXPECT_TRUE(rng.isInRange(parseInternetAddress(addr))) << addr << " not in " << rng.asString();
  }
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("2001:0db8:85a4::")));
}

} // Address
} // Network
