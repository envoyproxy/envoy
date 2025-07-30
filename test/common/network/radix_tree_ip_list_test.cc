// Comprehensive tests for RadixTree-based IP list implementation.

#include "source/common/network/radix_tree_ip_list.h"
#include "source/common/network/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace Address {

class RadixTreeIpListTest : public testing::Test {
protected:
  // Helper to create CIDR range protobuf.
  envoy::config::core::v3::CidrRange createCidrRange(const std::string& address_prefix,
                                                     uint32_t prefix_len) {
    envoy::config::core::v3::CidrRange range;
    range.set_address_prefix(address_prefix);
    range.mutable_prefix_len()->set_value(prefix_len);
    return range;
  }

  // Helper to create IP address instance using actual IP parsing.
  InstanceConstSharedPtr createAddress(const std::string& address) {
    return Network::Utility::parseInternetAddressNoThrow(address);
  }
};

// Test empty IP list creation and error handling.
TEST_F(RadixTreeIpListTest, EmptyIpListCreation) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> empty_ranges;

  auto result = RadixTreeIpList::create(empty_ranges);
  ASSERT_TRUE(result.ok());

  auto ip_list = std::move(result.value());
  EXPECT_EQ(ip_list->getIpListSize(), 0);

  // Any IP should return false for empty list.
  auto test_ip = createAddress("192.168.1.1");
  EXPECT_FALSE(ip_list->contains(*test_ip));
}

// Test single IPv4 range.
TEST_F(RadixTreeIpListTest, SingleIPv4Range) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("192.168.1.0", 24);

  auto result = RadixTreeIpList::create(ranges);
  ASSERT_TRUE(result.ok());

  auto ip_list = std::move(result.value());
  EXPECT_EQ(ip_list->getIpListSize(), 1);

  // Test addresses within range.
  EXPECT_TRUE(ip_list->contains(*createAddress("192.168.1.1")));
  EXPECT_TRUE(ip_list->contains(*createAddress("192.168.1.100")));
  EXPECT_TRUE(ip_list->contains(*createAddress("192.168.1.255")));

  // Test addresses outside range.
  EXPECT_FALSE(ip_list->contains(*createAddress("192.168.0.255")));
  EXPECT_FALSE(ip_list->contains(*createAddress("192.168.2.1")));
  EXPECT_FALSE(ip_list->contains(*createAddress("10.0.0.1")));
}

// Test multiple IPv4 ranges.
TEST_F(RadixTreeIpListTest, MultipleIPv4Ranges) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("192.168.1.0", 24); // Corporate network.
  *ranges.Add() = createCidrRange("10.0.0.0", 16);    // Private network.
  *ranges.Add() = createCidrRange("172.16.0.0", 12);  // Docker networks.
  *ranges.Add() = createCidrRange("203.0.113.0", 24); // Test network.

  auto result = RadixTreeIpList::create(ranges);
  ASSERT_TRUE(result.ok());

  auto ip_list = std::move(result.value());
  EXPECT_EQ(ip_list->getIpListSize(), 4);

  // Test addresses from each range.
  EXPECT_TRUE(ip_list->contains(*createAddress("192.168.1.50")));
  EXPECT_TRUE(ip_list->contains(*createAddress("10.0.5.100")));
  EXPECT_TRUE(ip_list->contains(*createAddress("172.16.10.5")));
  EXPECT_TRUE(ip_list->contains(*createAddress("203.0.113.200")));

  // Test addresses outside all ranges.
  EXPECT_FALSE(ip_list->contains(*createAddress("8.8.8.8")));
  EXPECT_FALSE(ip_list->contains(*createAddress("1.1.1.1")));
  EXPECT_FALSE(ip_list->contains(*createAddress("192.168.2.1")));
}

// Test invalid CIDR ranges.
TEST_F(RadixTreeIpListTest, InvalidCidrRanges) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;

  // Invalid IP address.
  *ranges.Add() = createCidrRange("invalid-ip", 24);

  auto result = RadixTreeIpList::create(ranges);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("invalid ip/mask combo"));
}

// Test single IPv6 range.
TEST_F(RadixTreeIpListTest, SingleIPv6Range) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("2001:db8::", 32);

  auto result = RadixTreeIpList::create(ranges);
  ASSERT_TRUE(result.ok());

  auto ip_list = std::move(result.value());
  EXPECT_EQ(ip_list->getIpListSize(), 1);

  // Test addresses within range.
  auto ipv6_addr1 = createAddress("2001:db8::1");
  auto ipv6_addr2 = createAddress("2001:db8:ffff::1");
  ASSERT_NE(ipv6_addr1, nullptr);
  ASSERT_NE(ipv6_addr2, nullptr);
  EXPECT_TRUE(ip_list->contains(*ipv6_addr1));
  EXPECT_TRUE(ip_list->contains(*ipv6_addr2));

  // Test addresses outside range.
  auto ipv6_outside1 = createAddress("2001:db9::1");
  auto ipv6_outside2 = createAddress("fe80::1");
  ASSERT_NE(ipv6_outside1, nullptr);
  ASSERT_NE(ipv6_outside2, nullptr);
  EXPECT_FALSE(ip_list->contains(*ipv6_outside1));
  EXPECT_FALSE(ip_list->contains(*ipv6_outside2));
}

// Test mixed IPv4 and IPv6 ranges.
TEST_F(RadixTreeIpListTest, MixedIPv4IPv6Ranges) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("192.168.1.0", 24);
  *ranges.Add() = createCidrRange("2001:db8::", 32);
  *ranges.Add() = createCidrRange("10.0.0.0", 8);
  *ranges.Add() = createCidrRange("fe80::", 10);

  auto result = RadixTreeIpList::create(ranges);
  ASSERT_TRUE(result.ok());

  auto ip_list = std::move(result.value());
  EXPECT_EQ(ip_list->getIpListSize(), 4);

  // Test IPv4 addresses.
  auto ipv4_addr1 = createAddress("192.168.1.100");
  auto ipv4_addr2 = createAddress("10.255.255.255");
  auto ipv4_outside = createAddress("172.16.1.1");
  ASSERT_NE(ipv4_addr1, nullptr);
  ASSERT_NE(ipv4_addr2, nullptr);
  ASSERT_NE(ipv4_outside, nullptr);
  EXPECT_TRUE(ip_list->contains(*ipv4_addr1));
  EXPECT_TRUE(ip_list->contains(*ipv4_addr2));
  EXPECT_FALSE(ip_list->contains(*ipv4_outside));

  // Test IPv6 addresses.
  auto ipv6_addr1 = createAddress("2001:db8::1");
  auto ipv6_addr2 = createAddress("fe80::1");
  auto ipv6_outside = createAddress("2600::1");
  ASSERT_NE(ipv6_addr1, nullptr);
  ASSERT_NE(ipv6_addr2, nullptr);
  ASSERT_NE(ipv6_outside, nullptr);
  EXPECT_TRUE(ip_list->contains(*ipv6_addr1));
  EXPECT_TRUE(ip_list->contains(*ipv6_addr2));
  EXPECT_FALSE(ip_list->contains(*ipv6_outside));
}

// Test large number of ranges for performance characteristics.
TEST_F(RadixTreeIpListTest, LargeNumberOfRanges) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;

  // Create 1000 ranges to test RadixTree performance.
  for (int i = 0; i < 1000; ++i) {
    *ranges.Add() = createCidrRange(fmt::format("10.{}.0.0", i % 256), 24);
  }

  auto result = RadixTreeIpList::create(ranges);
  ASSERT_TRUE(result.ok());

  auto ip_list = std::move(result.value());
  EXPECT_EQ(ip_list->getIpListSize(), 1000);

  // Test that matching still works correctly.
  auto addr1 = createAddress("10.50.0.100");
  auto addr2 = createAddress("192.168.1.1");
  ASSERT_NE(addr1, nullptr);
  ASSERT_NE(addr2, nullptr);
  EXPECT_TRUE(ip_list->contains(*addr1));
  EXPECT_FALSE(ip_list->contains(*addr2));
}

// Test non-IP address types (should return false).
TEST_F(RadixTreeIpListTest, NonIpAddressTypes) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("192.168.1.0", 24);

  auto result = RadixTreeIpList::create(ranges);
  ASSERT_TRUE(result.ok());

  auto ip_list = std::move(result.value());

  // Create a pipe address (non-IP type).
  auto pipe_result = Network::Address::PipeInstance::create("/tmp/test.sock");
  ASSERT_TRUE(pipe_result.ok());
  auto pipe_address = std::move(pipe_result.value());
  EXPECT_FALSE(ip_list->contains(*pipe_address));
}

} // namespace Address
} // namespace Network
} // namespace Envoy
