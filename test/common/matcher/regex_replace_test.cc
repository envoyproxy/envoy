#include "source/common/matcher/regex_replace.h"

#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

using ::Envoy::StatusHelpers::IsOk;
using ::testing::Eq;
using ::testing::Not;

class RegexReplaceTest : public testing::Test {
protected:
  Regex::GoogleReEngine engine_;
};

TEST_F(RegexReplaceTest, PerformsSubstitution) {
  ::envoy::type::matcher::v3::RegexMatchAndSubstitute proto;
  proto.mutable_pattern()->set_regex("abc");
  proto.set_substitution("xyz");
  auto regex_or = RegexReplace::create(engine_, proto);
  ASSERT_OK(regex_or.status());
  EXPECT_THAT(regex_or->apply("123abc123"), Eq("123xyz123"));
}

TEST_F(RegexReplaceTest, PerformsMarkerSubstitution) {
  ::envoy::type::matcher::v3::RegexMatchAndSubstitute proto;
  proto.mutable_pattern()->set_regex("a.(.)");
  proto.set_substitution("d\\0\\1");
  auto regex_or = RegexReplace::create(engine_, proto);
  ASSERT_OK(regex_or.status());
  EXPECT_THAT(regex_or->apply("123abc123abc"), Eq("123dabcc123dabcc"));
}

TEST_F(RegexReplaceTest, ErrorsOnInvalidRegex) {
  ::envoy::type::matcher::v3::RegexMatchAndSubstitute proto;
  proto.mutable_pattern()->set_regex("\\x");
  proto.set_substitution("xyz");
  auto regex_or = RegexReplace::create(engine_, proto);
  EXPECT_THAT(regex_or, Not(IsOk()));
}

} // namespace Matcher
} // namespace Envoy
