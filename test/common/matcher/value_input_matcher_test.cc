#include "source/common/matcher/value_input_matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

TEST(ValueInputMatcher, TestMatch) {
  envoy::type::matcher::v3::StringMatcher matcher_proto;
  matcher_proto.set_exact("exact");

  StringInputMatcher matcher(matcher_proto);

  EXPECT_TRUE(matcher.match(MatchingDataType("exact")));
  EXPECT_FALSE(matcher.match(MatchingDataType("not")));
  EXPECT_FALSE(matcher.match(MatchingDataType(absl::monostate())));
}

} // namespace Matcher
} // namespace Envoy
