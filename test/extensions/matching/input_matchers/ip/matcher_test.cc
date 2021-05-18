#include "envoy/network/address.h"

#include "extensions/matching/input_matchers/ip/matcher.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

TEST(MatcherTest, TestV4) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(Network::Address::CidrRange::create("192.0.2.0", 24));
  ranges.emplace_back(Network::Address::CidrRange::create("10.0.0.0", 24));
  Matcher m(std::move(ranges));
  EXPECT_FALSE(m.match("192.0.1.255"));
  EXPECT_TRUE(m.match("192.0.2.0"));
  EXPECT_TRUE(m.match("192.0.2.1"));
  EXPECT_TRUE(m.match("192.0.2.255"));
  EXPECT_FALSE(m.match("9.255.255.255"));
  EXPECT_TRUE(m.match("10.0.0.0"));
  EXPECT_TRUE(m.match("10.0.0.255"));
  EXPECT_FALSE(m.match("10.0.1.0"));
}

TEST(MatcherTest, TestV6) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(Network::Address::CidrRange::create("::1/128"));
  ranges.emplace_back(Network::Address::CidrRange::create("2001::/16"));
  ranges.emplace_back(Network::Address::CidrRange::create("2002::/16"));
  Matcher m(std::move(ranges));

  EXPECT_FALSE(m.match("::"));
  EXPECT_TRUE(m.match("::1"));
  EXPECT_FALSE(m.match("::2"));

  EXPECT_FALSE(m.match("2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
  EXPECT_TRUE(m.match("2001::1"));
  EXPECT_TRUE(m.match("2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
  EXPECT_TRUE(m.match("2002::1"));
  EXPECT_TRUE(m.match("2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
  EXPECT_FALSE(m.match("2003::"));
}

TEST(MatcherTest, EmptyRanges) {
  Matcher m(std::vector<Network::Address::CidrRange>{});
  EXPECT_FALSE(m.match("192.0.2.0"));
}

TEST(MatcherTest, EmptyIP) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(Network::Address::CidrRange::create("192.0.2.0", 24));
  Matcher m(std::move(ranges));
  EXPECT_FALSE(m.match(""));
  EXPECT_FALSE(m.match(absl::optional<absl::string_view>{}));
}

TEST(MatcherTest, InvalidIP) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(Network::Address::CidrRange::create("192.0.2.0", 24));
  Matcher m(std::move(ranges));
  EXPECT_THROW_WITH_MESSAGE(m.match("foo"), EnvoyException, "malformed IP address: foo");
}

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
