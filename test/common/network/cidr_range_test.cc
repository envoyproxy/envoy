#include <sys/un.h>

#include <iostream>
#include <string>

#include "envoy/common/exception.h"

#include "common/network/address_impl.h"
#include "common/network/cidr_range.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

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
    InstanceConstSharedPtr inPtr = parseInternetAddress(kv.first.first);
    EXPECT_NE(inPtr, nullptr) << kv.first.first;
    int length_io = kv.first.second;
    InstanceConstSharedPtr outPtr = CidrRange::truncateIpAddressAndLength(inPtr, &length_io);
    if (kv.second.second == -1) {
      EXPECT_EQ(outPtr, nullptr) << outPtr->asString() << "\n" << kv;
      EXPECT_EQ(length_io, -1) << kv;
    } else {
      ASSERT_NE(outPtr, nullptr) << kv;
      EXPECT_EQ(outPtr->ip()->addressAsString(), kv.second.first) << kv;
      EXPECT_EQ(length_io, kv.second.second) << kv;
    }
  }
}

TEST(Ipv4CidrRangeTest, InstanceConstSharedPtrAndLengthCtor) {
  InstanceConstSharedPtr ptr = parseInternetAddress("1.2.3.5");
  CidrRange rng(CidrRange::create(ptr, 31)); // Copy ctor.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.length(), 31);
  EXPECT_EQ(rng.version(), IpVersion::v4);
  EXPECT_EQ(rng.asString(), "1.2.3.4/31");
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.3")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.4")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.5")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.6")));

  CidrRange rng2(CidrRange::create(ptr, -1)); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  ptr.reset();
  CidrRange rng3(CidrRange::create(ptr, 10)); // Invalid address.
  EXPECT_FALSE(rng3.isValid());
}

TEST(Ipv4CidrRangeTest, StringAndLengthCtor) {
  CidrRange rng;
  rng = CidrRange::create("1.2.3.4", 31); // Assignment operator.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "1.2.3.4/31");
  EXPECT_EQ(rng.length(), 31);
  EXPECT_EQ(rng.version(), IpVersion::v4);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.3")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.4")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.5")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.6")));

  rng = CidrRange::create("1.2.3.4", -10); // Invalid length.
  EXPECT_FALSE(rng.isValid());

  EXPECT_THROW(CidrRange::create("bogus", 31), EnvoyException); // Invalid address.
}

TEST(Ipv4CidrRangeTest, StringCtor) {
  CidrRange rng = CidrRange::create("1.2.3.4/31");
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "1.2.3.4/31");
  EXPECT_EQ(rng.length(), 31);
  EXPECT_EQ(rng.version(), IpVersion::v4);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.3")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.4")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("1.2.3.5")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("1.2.3.6")));

EXPECT_THROW( CidrRange::create("1.2.3.4/-10"); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  CidrRange rng3 = CidrRange::create("bogus/31"); // Invalid address.
  EXPECT_FALSE(rng3.isValid());

  CidrRange rng4 = CidrRange::create("/31"); // Missing address.
  EXPECT_FALSE(rng4.isValid());

  CidrRange rng5 = CidrRange::create("1.2.3.4/"); // Missing length.
  EXPECT_FALSE(rng5.isValid());
}

TEST(Ipv4CidrRangeTest, BigRange) {
  CidrRange rng = CidrRange::create("10.255.255.255/8");
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

TEST(Ipv6CidrRange, InstanceConstSharedPtrAndLengthCtor) {
  InstanceConstSharedPtr ptr = parseInternetAddress("abcd::0345");
  CidrRange rng(CidrRange::create(ptr, 127)); // Copy ctor.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.length(), 127);
  EXPECT_EQ(rng.version(), IpVersion::v6);
  EXPECT_EQ(rng.asString(), "abcd::344/127");
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("abcd::343")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("abcd::344")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("abcd::345")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("abcd::346")));

  CidrRange rng2(CidrRange::create(ptr, -1)); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  ptr.reset();
  CidrRange rng3(CidrRange::create(ptr, 127)); // Invalid address.
  EXPECT_FALSE(rng3.isValid());
}

TEST(Ipv6CidrRange, StringAndLengthCtor) {
  CidrRange rng;
  rng = CidrRange::create("::ffff", 122); // Assignment operator.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "::ffc0/122");
  EXPECT_EQ(rng.length(), 122);
  EXPECT_EQ(rng.version(), IpVersion::v6);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("::ffbf")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("::ffc0")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("::ffff")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("::1:0")));

  rng = CidrRange::create("::ffff", -2); // Invalid length.
  EXPECT_FALSE(rng.isValid());

  rng = CidrRange::create("bogus", 122); // Invalid address.
  EXPECT_FALSE(rng.isValid());
}

TEST(Ipv6CidrRange, StringCtor) {
  CidrRange rng = CidrRange::create("::fc1f/118");
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "::fc00/118");
  EXPECT_EQ(rng.length(), 118);
  EXPECT_EQ(rng.version(), IpVersion::v6);
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("::fbff")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("::fc00")));
  EXPECT_TRUE(rng.isInRange(parseInternetAddress("::ffff")));
  EXPECT_FALSE(rng.isInRange(parseInternetAddress("::1:00")));

  CidrRange rng2 = CidrRange::create("::fc1f/-10"); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  CidrRange rng3 = CidrRange::create("::fc1f00/118"); // Invalid address.
  EXPECT_FALSE(rng3.isValid());

  CidrRange rng4 = CidrRange::create("/118"); // Missing address.
  EXPECT_FALSE(rng4.isValid());

  CidrRange rng5 = CidrRange::create("::fc1f/"); // Missing length.
  EXPECT_FALSE(rng5.isValid());
}

TEST(Ipv6CidrRange, BigRange) {
  std::string prefix = "2001:0db8:85a3:0000";
  CidrRange rng = CidrRange::create(prefix + "::/64");
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
