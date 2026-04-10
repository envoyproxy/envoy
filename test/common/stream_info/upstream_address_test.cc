#include "source/common/network/address_impl.h"
#include "source/common/stream_info/upstream_address.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

TEST(UpstreamAddressTest, KeyIsCorrect) {
  EXPECT_EQ("envoy.stream.upstream_address", UpstreamAddress::key());
}

TEST(UpstreamAddressTest, SerializeIPv4) {
  auto addr = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 8080);
  UpstreamAddress upstream_address(addr);
  auto result = upstream_address.serializeAsString();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("1.2.3.4:8080", result.value());
}

TEST(UpstreamAddressTest, SerializeIPv6) {
  auto addr = std::make_shared<Network::Address::Ipv6Instance>("::1", 9090);
  UpstreamAddress upstream_address(addr);
  auto result = upstream_address.serializeAsString();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("[::1]:9090", result.value());
}

TEST(UpstreamAddressTest, SerializeNullAddress) {
  UpstreamAddress upstream_address(nullptr);
  auto result = upstream_address.serializeAsString();
  EXPECT_FALSE(result.has_value());
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
