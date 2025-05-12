#include "envoy/network/address.h"

#include "source/extensions/matching/input_matchers/ip/matcher.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

class MatcherTest : public testing::Test {
public:
  void initialize(std::vector<Network::Address::CidrRange>&& ranges) {
    m_ = std::make_unique<Matcher>(std::move(ranges), stat_prefix_, scope_);
  }

  Stats::TestUtil::TestStore store_;
  Stats::Scope& scope_{*store_.rootScope()};
  std::string stat_prefix_{"ipmatcher.test"};
  std::unique_ptr<Matcher> m_;
};

TEST_F(MatcherTest, TestV4) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(*Network::Address::CidrRange::create("192.0.2.0", 24));
  ranges.emplace_back(*Network::Address::CidrRange::create("10.0.0.0", 24));
  initialize(std::move(ranges));
  EXPECT_FALSE(m_->match("192.0.1.255"));
  EXPECT_TRUE(m_->match("192.0.2.0"));
  EXPECT_TRUE(m_->match("192.0.2.1"));
  EXPECT_TRUE(m_->match("192.0.2.255"));
  EXPECT_FALSE(m_->match("9.255.255.255"));
  EXPECT_TRUE(m_->match("10.0.0.0"));
  EXPECT_TRUE(m_->match("10.0.0.255"));
  EXPECT_FALSE(m_->match("10.0.1.0"));
}

TEST_F(MatcherTest, TestV6) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(*Network::Address::CidrRange::create("::1/128"));
  ranges.emplace_back(*Network::Address::CidrRange::create("2001::/16"));
  ranges.emplace_back(*Network::Address::CidrRange::create("2002::/16"));
  initialize(std::move(ranges));

  EXPECT_FALSE(m_->match("::"));
  EXPECT_TRUE(m_->match("::1"));
  EXPECT_FALSE(m_->match("::2"));

  EXPECT_FALSE(m_->match("2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
  EXPECT_TRUE(m_->match("2001::1"));
  EXPECT_TRUE(m_->match("2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
  EXPECT_TRUE(m_->match("2002::1"));
  EXPECT_TRUE(m_->match("2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
  EXPECT_FALSE(m_->match("2003::"));
}

TEST_F(MatcherTest, EmptyRanges) {
  initialize(std::vector<Network::Address::CidrRange>{});
  EXPECT_FALSE(m_->match("192.0.2.0"));
}

TEST_F(MatcherTest, EmptyIP) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(*Network::Address::CidrRange::create("192.0.2.0", 24));
  initialize(std::move(ranges));
  EXPECT_FALSE(m_->match(""));
  EXPECT_FALSE(m_->match(absl::monostate()));
}

TEST_F(MatcherTest, InvalidIP) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(*Network::Address::CidrRange::create("192.0.2.0", 24));
  initialize(std::move(ranges));
  EXPECT_EQ(m_->stats()->ip_parsing_failed_.value(), 0);
  EXPECT_FALSE(m_->match("foo"));
  EXPECT_EQ(m_->stats()->ip_parsing_failed_.value(), 1);
}

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
