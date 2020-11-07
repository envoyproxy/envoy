#include <iostream>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"

#include "common/common/fmt.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/network/cidr_range.h"
#include "common/network/utility.h"

#include "gtest/gtest.h"

// We are adding things into the std namespace.
// Note that this is technically undefined behavior!
namespace std {

// Pair
template <typename First, typename Second>
std::ostream& operator<<(std::ostream& out, const std::pair<First, Second>& p) {
  return out << '(' << p.first << ", " << p.second << ')';
}

} // namespace std

namespace Envoy {

namespace Network {
namespace Address {
namespace {

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
    InstanceConstSharedPtr inPtr = Utility::parseInternetAddress(kv.first.first);
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

TEST(IsInRange, Various) {
  {
    CidrRange rng = CidrRange::create("foo");
    EXPECT_FALSE(rng.isValid());
    EXPECT_FALSE(rng.isInRange(Ipv4Instance("0.0.0.0")));
  }

  {
    CidrRange rng = CidrRange::create("10.255.255.255/0");
    EXPECT_TRUE(rng.isValid());
    EXPECT_EQ(rng.asString(), "0.0.0.0/0");
    EXPECT_EQ(rng.length(), 0);
    EXPECT_EQ(rng.ip()->version(), IpVersion::v4);
    EXPECT_TRUE(rng.isInRange(Ipv4Instance("10.255.255.255")));
    EXPECT_TRUE(rng.isInRange(Ipv4Instance("9.255.255.255")));
    EXPECT_TRUE(rng.isInRange(Ipv4Instance("0.0.0.0")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("::")));
    EXPECT_FALSE(rng.isInRange(PipeInstance("foo")));
  }

  {
    CidrRange rng = CidrRange::create("10.255.255.255/10");
    EXPECT_TRUE(rng.isValid());
    EXPECT_EQ(rng.asString(), "10.192.0.0/10");
    EXPECT_EQ(rng.length(), 10);
    EXPECT_EQ(rng.ip()->version(), IpVersion::v4);
    EXPECT_TRUE(rng.isInRange(Ipv4Instance("10.255.255.255")));
    EXPECT_FALSE(rng.isInRange(Ipv4Instance("9.255.255.255")));
    EXPECT_FALSE(rng.isInRange(Ipv4Instance("0.0.0.0")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("::")));
  }

  {
    CidrRange rng = CidrRange::create("::/0");
    EXPECT_TRUE(rng.isValid());
    EXPECT_EQ(rng.asString(), "::/0");
    EXPECT_EQ(rng.length(), 0);
    EXPECT_EQ(rng.ip()->version(), IpVersion::v6);
    EXPECT_TRUE(rng.isInRange(Ipv6Instance("::")));
    EXPECT_TRUE(rng.isInRange(Ipv6Instance("::1")));
    EXPECT_TRUE(rng.isInRange(Ipv6Instance("2001::")));
    EXPECT_FALSE(rng.isInRange(Ipv4Instance("0.0.0.0")));
    EXPECT_FALSE(rng.isInRange(PipeInstance("foo")));
  }

  {
    CidrRange rng = CidrRange::create("::1/128");
    EXPECT_TRUE(rng.isValid());
    EXPECT_EQ(rng.asString(), "::1/128");
    EXPECT_EQ(rng.length(), 128);
    EXPECT_EQ(rng.ip()->version(), IpVersion::v6);
    EXPECT_TRUE(rng.isInRange(Ipv6Instance("::1")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001::")));
    EXPECT_FALSE(rng.isInRange(Ipv4Instance("0.0.0.0")));
  }

  {
    CidrRange rng = CidrRange::create("2001:abcd:ef01:2345:6789:abcd:ef01:234/64");
    EXPECT_TRUE(rng.isValid());
    EXPECT_EQ(rng.asString(), "2001:abcd:ef01:2345::/64");
    EXPECT_EQ(rng.length(), 64);
    EXPECT_EQ(rng.ip()->version(), IpVersion::v6);
    EXPECT_TRUE(rng.isInRange(Ipv6Instance("2001:abcd:ef01:2345::1")));
    EXPECT_TRUE(rng.isInRange(Ipv6Instance("2001:abcd:ef01:2345::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001:abcd::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001:abcd:ef01:2340::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2002::")));
  }

  {
    CidrRange rng = CidrRange::create("2001:abcd:ef01:2345:6789:abcd:ef01:234/60");
    EXPECT_TRUE(rng.isValid());
    EXPECT_EQ(rng.asString(), "2001:abcd:ef01:2340::/60");
    EXPECT_EQ(rng.length(), 60);
    EXPECT_EQ(rng.ip()->version(), IpVersion::v6);
    EXPECT_TRUE(rng.isInRange(Ipv6Instance("2001:abcd:ef01:2345::")));
    EXPECT_TRUE(rng.isInRange(Ipv6Instance("2001:abcd:ef01:2340::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001:abcd:ef01:2330::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001:abcd::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001:abcd:ef00::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001::")));
    EXPECT_FALSE(rng.isInRange(Ipv6Instance("2002::")));
  }
}

TEST(CidrRangeTest, OperatorIsEqual) {
  {
    CidrRange rng1 = CidrRange::create("192.0.0.0/8");
    CidrRange rng2 = CidrRange::create("192.168.0.0/16");
    EXPECT_FALSE(rng1 == rng2);
  }

  {
    CidrRange rng1 = CidrRange::create("192.0.0.0/8");
    CidrRange rng2 = CidrRange::create("192.168.0.0/8");
    EXPECT_TRUE(rng1 == rng2);
  }

  {
    CidrRange rng1 = CidrRange::create("192.0.0.0/8");
    CidrRange rng2 = CidrRange::create("2001::/8");
    EXPECT_FALSE(rng1 == rng2);
  }

  {
    CidrRange rng1 = CidrRange::create("2002::/16");
    CidrRange rng2 = CidrRange::create("2001::/16");
    EXPECT_FALSE(rng1 == rng2);
  }

  {
    CidrRange rng1 = CidrRange::create("2002::/16");
    CidrRange rng2 = CidrRange::create("192.168.0.1/16");
    EXPECT_FALSE(rng1 == rng2);
  }

  {
    CidrRange rng1 = CidrRange::create("2002::/16");
    CidrRange rng2 = CidrRange::create("2002::1/16");
    EXPECT_TRUE(rng1 == rng2);
  }
}

TEST(CidrRangeTest, InvalidCidrRange) {
  CidrRange rng1 = CidrRange::create("foo");
  EXPECT_EQ(nullptr, rng1.ip());
  EXPECT_EQ("/-1", rng1.asString());
  // Not equal due to invalid CidrRange.
  EXPECT_FALSE(rng1 == rng1);

  CidrRange rng2 = CidrRange::create("192.0.0.0/8");
  EXPECT_FALSE(rng1 == rng2);
}

TEST(Ipv4CidrRangeTest, InstanceConstSharedPtrAndLengthCtor) {
  InstanceConstSharedPtr ptr = Utility::parseInternetAddress("1.2.3.5");
  CidrRange rng(CidrRange::create(ptr, 31)); // Copy ctor.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.length(), 31);
  EXPECT_EQ(rng.ip()->version(), IpVersion::v4);
  EXPECT_EQ(rng.asString(), "1.2.3.4/31");
  EXPECT_FALSE(rng.isInRange(Ipv4Instance("1.2.3.3")));
  EXPECT_TRUE(rng.isInRange(Ipv4Instance("1.2.3.4")));
  EXPECT_TRUE(rng.isInRange(Ipv4Instance("1.2.3.5")));
  EXPECT_FALSE(rng.isInRange(Ipv4Instance("1.2.3.6")));

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
  EXPECT_EQ(rng.ip()->version(), IpVersion::v4);
  EXPECT_FALSE(rng.isInRange(Ipv4Instance("1.2.3.3")));
  EXPECT_TRUE(rng.isInRange(Ipv4Instance("1.2.3.4")));
  EXPECT_TRUE(rng.isInRange(Ipv4Instance("1.2.3.5")));
  EXPECT_FALSE(rng.isInRange(Ipv4Instance("1.2.3.6")));

  rng = CidrRange::create("1.2.3.4", -10); // Invalid length.
  EXPECT_FALSE(rng.isValid());

  EXPECT_THROW(CidrRange::create("bogus", 31), EnvoyException); // Invalid address.
}

TEST(Ipv4CidrRangeTest, StringCtor) {
  CidrRange rng = CidrRange::create("1.2.3.4/31");
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "1.2.3.4/31");
  EXPECT_EQ(rng.length(), 31);
  EXPECT_EQ(rng.ip()->version(), IpVersion::v4);
  EXPECT_FALSE(rng.isInRange(Ipv4Instance("1.2.3.3")));
  EXPECT_TRUE(rng.isInRange(Ipv4Instance("1.2.3.4")));
  EXPECT_TRUE(rng.isInRange(Ipv4Instance("1.2.3.5")));
  EXPECT_FALSE(rng.isInRange(Ipv4Instance("1.2.3.6")));

  CidrRange rng2 = CidrRange::create("1.2.3.4/-10"); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  EXPECT_THROW(CidrRange::create("bogus/31"), EnvoyException); // Invalid address.

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
  EXPECT_EQ(rng.ip()->version(), IpVersion::v4);
  EXPECT_FALSE(rng.isInRange(Ipv4Instance("9.255.255.255")));
  std::string addr;
  for (int i = 0; i < 256; ++i) {
    addr = fmt::format("10.{}.0.1", i);
    EXPECT_TRUE(rng.isInRange(Ipv4Instance(addr))) << addr;
    addr = fmt::format("10.{}.255.255", i);
    EXPECT_TRUE(rng.isInRange(Ipv4Instance(addr))) << addr;
  }
  EXPECT_FALSE(rng.isInRange(Ipv4Instance("11.0.0.0")));
}

TEST(Ipv6CidrRange, InstanceConstSharedPtrAndLengthCtor) {
  InstanceConstSharedPtr ptr = Utility::parseInternetAddress("abcd::0345");
  CidrRange rng(CidrRange::create(ptr, 127)); // Copy ctor.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.length(), 127);
  EXPECT_EQ(rng.ip()->version(), IpVersion::v6);
  EXPECT_EQ(rng.asString(), "abcd::344/127");
  EXPECT_FALSE(rng.isInRange(Ipv6Instance("abcd::343")));
  EXPECT_TRUE(rng.isInRange(Ipv6Instance("abcd::344")));
  EXPECT_TRUE(rng.isInRange(Ipv6Instance("abcd::345")));
  EXPECT_FALSE(rng.isInRange(Ipv6Instance("abcd::346")));

  CidrRange rng2(CidrRange::create(ptr, -1)); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  ptr.reset();
  CidrRange rng3(CidrRange::create(ptr, 127)); // Invalid address.
  EXPECT_FALSE(rng3.isValid());
}

TEST(Ipv6CidrRange, StringAndLengthCtor) {
  CidrRange rng;
  rng = CidrRange::create("ff::ffff", 122); // Assignment operator.
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "ff::ffc0/122");
  EXPECT_EQ(rng.length(), 122);
  EXPECT_EQ(rng.ip()->version(), IpVersion::v6);
  EXPECT_FALSE(rng.isInRange(Ipv6Instance("ff::ffbf")));
  EXPECT_TRUE(rng.isInRange(Ipv6Instance("ff::ffc0")));
  EXPECT_TRUE(rng.isInRange(Ipv6Instance("ff::ffff")));
  EXPECT_FALSE(rng.isInRange(Ipv6Instance("::1:0")));

  rng = CidrRange::create("::ffff", -2); // Invalid length.
  EXPECT_FALSE(rng.isValid());

  EXPECT_THROW(CidrRange::create("bogus", 122), EnvoyException); // Invalid address.
}

TEST(Ipv6CidrRange, StringCtor) {
  CidrRange rng = CidrRange::create("ff::fc1f/118");
  EXPECT_TRUE(rng.isValid());
  EXPECT_EQ(rng.asString(), "ff::fc00/118");
  EXPECT_EQ(rng.length(), 118);
  EXPECT_EQ(rng.ip()->version(), IpVersion::v6);
  EXPECT_FALSE(rng.isInRange(Ipv6Instance("ff::fbff")));
  EXPECT_TRUE(rng.isInRange(Ipv6Instance("ff::fc00")));
  EXPECT_TRUE(rng.isInRange(Ipv6Instance("ff::ffff")));
  EXPECT_FALSE(rng.isInRange(Ipv6Instance("::1:00")));

  CidrRange rng2 = CidrRange::create("::fc1f/-10"); // Invalid length.
  EXPECT_FALSE(rng2.isValid());

  EXPECT_THROW(CidrRange::create("::fc1f00/118"), EnvoyException); // Invalid address.

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
  EXPECT_EQ(rng.ip()->version(), IpVersion::v6);
  EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001:0db8:85a2:ffff:ffff:ffff:ffff:ffff")));
  std::string addr;
  for (char c : std::string("0123456789abcdef")) {
    addr = fmt::format("{}:000{}::", prefix, std::string(1, c));
    EXPECT_TRUE(rng.isInRange(Ipv6Instance(addr))) << addr << " not in " << rng.asString();
    addr = fmt::format("{}:fff{}:ffff:ffff:ffff", prefix, std::string(1, c));
    EXPECT_TRUE(rng.isInRange(Ipv6Instance(addr))) << addr << " not in " << rng.asString();
  }
  EXPECT_FALSE(rng.isInRange(Ipv6Instance("2001:0db8:85a4::")));
}

Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>
makeCidrRangeList(const std::vector<std::pair<std::string, uint32_t>>& ranges) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ret;
  for (auto& range : ranges) {
    auto new_element = ret.Add();
    new_element->set_address_prefix(range.first);
    new_element->mutable_prefix_len()->set_value(range.second);
  }
  return ret;
}

TEST(IpListTest, Errors) {
  {
    EXPECT_THROW({ IpList list(makeCidrRangeList({{"foo", 0}})); }, EnvoyException);
  }
}

TEST(IpListTest, SpecificAddressAllowed) {
  IpList list(makeCidrRangeList({{"192.168.1.1", 24}}));

  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.1.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.1.3")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.1.255")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("192.168.3.0")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("192.168.0.0")));
}

TEST(IpListTest, Normal) {
  IpList list(makeCidrRangeList({{"192.168.3.0", 24}, {"50.1.2.3", 32}, {"10.15.0.0", 16}}));

  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.3")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.255")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("192.168.2.255")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("192.168.4.0")));

  EXPECT_TRUE(list.contains(Address::Ipv4Instance("50.1.2.3")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("50.1.2.2")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("50.1.2.4")));

  EXPECT_TRUE(list.contains(Address::Ipv4Instance("10.15.0.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("10.15.90.90")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("10.15.255.255")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("10.14.255.255")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("10.16.0.0")));

  EXPECT_FALSE(list.contains(Address::Ipv6Instance("::1")));
  EXPECT_FALSE(list.contains(Address::PipeInstance("foo")));
}

TEST(IpListTest, AddressVersionMix) {
  IpList list(makeCidrRangeList({{"192.168.3.0", 24}, {"2001:db8:85a3::", 64}, {"::1", 128}}));

  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.3")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.255")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("192.168.2.255")));
  EXPECT_FALSE(list.contains(Address::Ipv4Instance("192.168.4.0")));

  EXPECT_TRUE(list.contains(Address::Ipv6Instance("2001:db8:85a3::")));
  EXPECT_TRUE(list.contains(Address::Ipv6Instance("2001:db8:85a3:0:1::")));
  EXPECT_TRUE(list.contains(Address::Ipv6Instance("2001:db8:85a3::ffff:ffff:ffff:ffff")));
  EXPECT_TRUE(list.contains(Address::Ipv6Instance("2001:db8:85a3::ffff")));
  EXPECT_TRUE(list.contains(Address::Ipv6Instance("2001:db8:85a3::1")));
  EXPECT_FALSE(list.contains(Address::Ipv6Instance("2001:db8:85a3:1::")));
  EXPECT_FALSE(list.contains(Address::Ipv6Instance("2002:db8:85a3::")));

  EXPECT_TRUE(list.contains(Address::Ipv6Instance("::1")));
  EXPECT_FALSE(list.contains(Address::Ipv6Instance("::")));

  EXPECT_FALSE(list.contains(Address::PipeInstance("foo")));
}

TEST(IpListTest, MatchAny) {
  IpList list(makeCidrRangeList({{"0.0.0.0", 0}}));

  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.3")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.255")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.0.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.0.0.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("1.1.1.1")));

  EXPECT_FALSE(list.contains(Address::Ipv6Instance("::1")));
  EXPECT_FALSE(list.contains(Address::PipeInstance("foo")));
}

TEST(IpListTest, MatchAnyAll) {
  IpList list(makeCidrRangeList({{"0.0.0.0", 0}, {"::", 0}}));

  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.3")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.3.255")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.168.0.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("192.0.0.0")));
  EXPECT_TRUE(list.contains(Address::Ipv4Instance("1.1.1.1")));

  EXPECT_TRUE(list.contains(Address::Ipv6Instance("::1")));
  EXPECT_TRUE(list.contains(Address::Ipv6Instance("::")));
  EXPECT_TRUE(list.contains(Address::Ipv6Instance("2001:db8:85a3::")));
  EXPECT_TRUE(list.contains(Address::Ipv6Instance("ffee::")));

  EXPECT_FALSE(list.contains(Address::PipeInstance("foo")));
}

} // namespace
} // namespace Address
} // namespace Network
} // namespace Envoy
