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

using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::MatchResult;

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
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("192.0.1.255")), MatchResult::NoMatch);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("192.0.2.0")), MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("192.0.2.1")), MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("192.0.2.255")), MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("9.255.255.255")), MatchResult::NoMatch);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("10.0.0.0")), MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("10.0.0.255")), MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("10.0.1.0")), MatchResult::NoMatch);
}

TEST_F(MatcherTest, TestV6) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(*Network::Address::CidrRange::create("::1/128"));
  ranges.emplace_back(*Network::Address::CidrRange::create("2001::/16"));
  ranges.emplace_back(*Network::Address::CidrRange::create("2002::/16"));
  initialize(std::move(ranges));

  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("::")), MatchResult::NoMatch);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("::1")), MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("::2")), MatchResult::NoMatch);

  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff")),
            MatchResult::NoMatch);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("2001::1")), MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff")),
            MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("2002::1")), MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff")),
            MatchResult::Matched);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("2003::")), MatchResult::NoMatch);
}

TEST_F(MatcherTest, EmptyRanges) {
  initialize(std::vector<Network::Address::CidrRange>{});
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("192.0.2.0")), MatchResult::NoMatch);
}

TEST_F(MatcherTest, EmptyIP) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(*Network::Address::CidrRange::create("192.0.2.0", 24));
  initialize(std::move(ranges));
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("")), MatchResult::NoMatch);
  EXPECT_EQ(m_->match(DataInputGetResult::NoData()), MatchResult::NoMatch);
}

TEST_F(MatcherTest, InvalidIP) {
  std::vector<Network::Address::CidrRange> ranges;
  ranges.emplace_back(*Network::Address::CidrRange::create("192.0.2.0", 24));
  initialize(std::move(ranges));
  EXPECT_EQ(m_->stats()->ip_parsing_failed_.value(), 0);
  EXPECT_EQ(m_->match(DataInputGetResult::CreateString("foo")), MatchResult::NoMatch);
  EXPECT_EQ(m_->stats()->ip_parsing_failed_.value(), 1);
}

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
