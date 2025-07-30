// Comprehensive tests for RadixTree-enhanced IPMatcher in RBAC.

#include "source/common/network/utility.h"
#include "source/extensions/filters/common/rbac/matchers.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class RadixTreeIPMatcherTest : public testing::Test {
protected:
  // Helper to create CIDR range protobuf.
  envoy::config::core::v3::CidrRange createCidrRange(const std::string& address_prefix,
                                                     uint32_t prefix_len) {
    envoy::config::core::v3::CidrRange range;
    range.set_address_prefix(address_prefix);
    range.mutable_prefix_len()->set_value(prefix_len);
    return range;
  }

  // Helper to check matcher with mock connection setup.
  void checkIPMatcher(const IPMatcher& matcher, bool expected_match, const std::string& test_ip,
                      IPMatcher::Type type) {
    NiceMock<Network::MockConnection> conn;
    Envoy::Http::TestRequestHeaderMapImpl headers;
    NiceMock<StreamInfo::MockStreamInfo> info;

    auto addr = Network::Utility::parseInternetAddressNoThrow(test_ip);
    ASSERT_NE(addr, nullptr) << "Failed to parse IP: " << test_ip;

    // Setup connection and stream info based on matcher type.
    switch (type) {
    case IPMatcher::ConnectionRemote:
      conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr);
      break;
    case IPMatcher::DownstreamLocal:
      info.downstream_connection_info_provider_->setLocalAddress(addr);
      break;
    case IPMatcher::DownstreamDirectRemote:
      info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);
      break;
    case IPMatcher::DownstreamRemote:
      info.downstream_connection_info_provider_->setRemoteAddress(addr);
      break;
    }

    EXPECT_EQ(expected_match, matcher.matches(conn, headers, info))
        << "IP: " << test_ip << " Type: " << static_cast<int>(type);
  }
};

// Test single range constructor with actual IP matching.
TEST_F(RadixTreeIPMatcherTest, SingleRangeConstructorConnectionRemote) {
  auto range = createCidrRange("192.168.1.0", 24);
  IPMatcher matcher(range, IPMatcher::ConnectionRemote);

  // Test matching addresses within range.
  checkIPMatcher(matcher, true, "192.168.1.1", IPMatcher::ConnectionRemote);
  checkIPMatcher(matcher, true, "192.168.1.100", IPMatcher::ConnectionRemote);
  checkIPMatcher(matcher, true, "192.168.1.255", IPMatcher::ConnectionRemote);

  // Test non-matching addresses.
  checkIPMatcher(matcher, false, "192.168.2.1", IPMatcher::ConnectionRemote);
  checkIPMatcher(matcher, false, "10.0.0.1", IPMatcher::ConnectionRemote);
}

// Test single range constructor with IPv6.
TEST_F(RadixTreeIPMatcherTest, SingleRangeConstructorIPv6) {
  auto range = createCidrRange("2001:db8::", 32);
  IPMatcher matcher(range, IPMatcher::ConnectionRemote);

  // Test matching IPv6 addresses.
  checkIPMatcher(matcher, true, "2001:db8::1", IPMatcher::ConnectionRemote);
  checkIPMatcher(matcher, true, "2001:db8:ffff::1", IPMatcher::ConnectionRemote);

  // Test non-matching IPv6 addresses.
  checkIPMatcher(matcher, false, "2001:db9::1", IPMatcher::ConnectionRemote);
  checkIPMatcher(matcher, false, "fe80::1", IPMatcher::ConnectionRemote);
}

// Test multiple ranges with create() factory method and actual matching.
TEST_F(RadixTreeIPMatcherTest, MultipleRangesCreate) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("192.168.1.0", 24); // Corporate network.
  *ranges.Add() = createCidrRange("10.0.0.0", 16);    // Private network.
  *ranges.Add() = createCidrRange("172.16.0.0", 12);  // Docker networks.
  *ranges.Add() = createCidrRange("203.0.113.0", 24); // Test network.

  auto result = IPMatcher::create(ranges, IPMatcher::ConnectionRemote);
  ASSERT_TRUE(result.ok());
  auto matcher = std::move(result.value());

  // Test addresses from each range.
  checkIPMatcher(*matcher, true, "192.168.1.50", IPMatcher::ConnectionRemote);
  checkIPMatcher(*matcher, true, "10.0.5.100", IPMatcher::ConnectionRemote);
  checkIPMatcher(*matcher, true, "172.16.10.5", IPMatcher::ConnectionRemote);
  checkIPMatcher(*matcher, true, "203.0.113.200", IPMatcher::ConnectionRemote);

  // Test addresses outside all ranges.
  checkIPMatcher(*matcher, false, "8.8.8.8", IPMatcher::ConnectionRemote);
  checkIPMatcher(*matcher, false, "1.1.1.1", IPMatcher::ConnectionRemote);
  checkIPMatcher(*matcher, false, "192.168.2.1", IPMatcher::ConnectionRemote);
}

// Test all matcher types with actual IP matching.
TEST_F(RadixTreeIPMatcherTest, AllMatcherTypesMatching) {
  auto range = createCidrRange("192.168.1.0", 24);

  // Test ConnectionRemote type.
  IPMatcher matcher1(range, IPMatcher::ConnectionRemote);
  checkIPMatcher(matcher1, true, "192.168.1.100", IPMatcher::ConnectionRemote);
  checkIPMatcher(matcher1, false, "10.0.0.1", IPMatcher::ConnectionRemote);

  // Test DownstreamLocal type.
  IPMatcher matcher2(range, IPMatcher::DownstreamLocal);
  checkIPMatcher(matcher2, true, "192.168.1.100", IPMatcher::DownstreamLocal);
  checkIPMatcher(matcher2, false, "10.0.0.1", IPMatcher::DownstreamLocal);

  // Test DownstreamDirectRemote type.
  IPMatcher matcher3(range, IPMatcher::DownstreamDirectRemote);
  checkIPMatcher(matcher3, true, "192.168.1.100", IPMatcher::DownstreamDirectRemote);
  checkIPMatcher(matcher3, false, "10.0.0.1", IPMatcher::DownstreamDirectRemote);

  // Test DownstreamRemote type.
  IPMatcher matcher4(range, IPMatcher::DownstreamRemote);
  checkIPMatcher(matcher4, true, "192.168.1.100", IPMatcher::DownstreamRemote);
  checkIPMatcher(matcher4, false, "10.0.0.1", IPMatcher::DownstreamRemote);
}

// Test error handling for empty range list.
TEST_F(RadixTreeIPMatcherTest, EmptyRangeListError) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> empty_ranges;

  auto result = IPMatcher::create(empty_ranges, IPMatcher::ConnectionRemote);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Empty IP range list provided"));
}

// Test error handling for invalid CIDR range in single constructor.
TEST_F(RadixTreeIPMatcherTest, InvalidSingleRangeError) {
  auto invalid_range = createCidrRange("invalid-ip", 24);

  EXPECT_THROW(IPMatcher(invalid_range, IPMatcher::ConnectionRemote), EnvoyException);
}

// Test error handling for invalid CIDR range in create method.
TEST_F(RadixTreeIPMatcherTest, InvalidMultipleRangesError) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("192.168.1.0", 24); // Valid range.
  *ranges.Add() = createCidrRange("invalid-ip", 24);  // Invalid range.

  auto result = IPMatcher::create(ranges, IPMatcher::ConnectionRemote);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("invalid ip/mask combo"));
}

// Test successful creation with various valid prefix lengths.
TEST_F(RadixTreeIPMatcherTest, ValidPrefixLengths) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("192.168.1.0", 24); // Standard /24 network.
  *ranges.Add() = createCidrRange("10.0.0.0", 8);     // Large /8 network.
  *ranges.Add() = createCidrRange("172.16.0.0", 16);  // Medium /16 network.

  auto result = IPMatcher::create(ranges, IPMatcher::ConnectionRemote);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);
}

// Test single /32 host range.
TEST_F(RadixTreeIPMatcherTest, SingleHostRange) {
  auto range = createCidrRange("192.168.1.100", 32);
  IPMatcher matcher(range, IPMatcher::ConnectionRemote);

  // Test exact host match.
  checkIPMatcher(matcher, true, "192.168.1.100", IPMatcher::ConnectionRemote);

  // Test non-matching hosts.
  checkIPMatcher(matcher, false, "192.168.1.101", IPMatcher::ConnectionRemote);
  checkIPMatcher(matcher, false, "192.168.1.99", IPMatcher::ConnectionRemote);
}

// Test single /128 IPv6 host range.
TEST_F(RadixTreeIPMatcherTest, SingleIPv6HostRange) {
  auto range = createCidrRange("2001:db8::1", 128);
  IPMatcher matcher(range, IPMatcher::ConnectionRemote);

  // Test exact IPv6 host match.
  checkIPMatcher(matcher, true, "2001:db8::1", IPMatcher::ConnectionRemote);

  // Test non-matching IPv6 hosts.
  checkIPMatcher(matcher, false, "2001:db8::2", IPMatcher::ConnectionRemote);
}

// Test large number of ranges for performance characteristics.
TEST_F(RadixTreeIPMatcherTest, LargeNumberOfRanges) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;

  // Create 100 ranges to test RadixTree performance.
  for (int i = 0; i < 100; ++i) {
    *ranges.Add() = createCidrRange(fmt::format("10.{}.0.0", i), 24);
  }

  auto result = IPMatcher::create(ranges, IPMatcher::ConnectionRemote);
  ASSERT_TRUE(result.ok());
  auto matcher = std::move(result.value());

  // Test that matching still works correctly.
  checkIPMatcher(*matcher, true, "10.50.0.100", IPMatcher::ConnectionRemote);
  checkIPMatcher(*matcher, true, "10.99.0.50", IPMatcher::ConnectionRemote);

  // Test non-matching address.
  checkIPMatcher(*matcher, false, "192.168.1.1", IPMatcher::ConnectionRemote);
}

// Test overlapping ranges.
TEST_F(RadixTreeIPMatcherTest, OverlappingRanges) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("192.168.0.0", 16);   // Large range.
  *ranges.Add() = createCidrRange("192.168.1.0", 24);   // Subset of above.
  *ranges.Add() = createCidrRange("192.168.1.100", 32); // Single host in subset.

  auto result = IPMatcher::create(ranges, IPMatcher::ConnectionRemote);
  ASSERT_TRUE(result.ok());
  auto matcher = std::move(result.value());

  // Validates overlapping ranges RadixTree creation.
  EXPECT_NE(matcher, nullptr);
}

// Test mixed IPv4 and IPv6 ranges.
TEST_F(RadixTreeIPMatcherTest, MixedIPv4IPv6Ranges) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;
  *ranges.Add() = createCidrRange("192.168.1.0", 24);
  *ranges.Add() = createCidrRange("2001:db8::", 32);
  *ranges.Add() = createCidrRange("10.0.0.0", 8);
  *ranges.Add() = createCidrRange("fe80::", 10);

  auto result = IPMatcher::create(ranges, IPMatcher::ConnectionRemote);
  ASSERT_TRUE(result.ok());
  auto matcher = std::move(result.value());

  // Validates mixed IPv4/IPv6 RadixTree creation.
  EXPECT_NE(matcher, nullptr);
}

// Test boundary conditions.
TEST_F(RadixTreeIPMatcherTest, BoundaryConditions) {
  auto range = createCidrRange("192.168.1.0", 24); // 192.168.1.0-192.168.1.255
  IPMatcher matcher(range, IPMatcher::ConnectionRemote);

  // Test exact boundaries.
  checkIPMatcher(matcher, true, "192.168.1.0", IPMatcher::ConnectionRemote);   // Network address.
  checkIPMatcher(matcher, true, "192.168.1.255", IPMatcher::ConnectionRemote); // Broadcast address.

  // Test one outside each boundary.
  checkIPMatcher(matcher, false, "192.168.0.255", IPMatcher::ConnectionRemote);
  checkIPMatcher(matcher, false, "192.168.2.0", IPMatcher::ConnectionRemote);
}

// Test extractIpAddress coverage with nullptr handling.
TEST_F(RadixTreeIPMatcherTest, ExtractIpAddressNull) {
  auto range = createCidrRange("192.168.1.0", 24);
  IPMatcher matcher(range, IPMatcher::ConnectionRemote);

  NiceMock<Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  // Setup connection with no remote address (nullptr).
  conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(nullptr);

  EXPECT_FALSE(matcher.matches(conn, headers, info));
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
