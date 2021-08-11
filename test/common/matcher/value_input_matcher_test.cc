#include "source/common/matcher/value_input_matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

TEST(ValueInputMatcher, TestMatch) {
  envoy::type::matcher::v3::StringMatcher matcher_proto;
  matcher_proto.set_exact("exact");

  StringInputMatcher matcher(matcher_proto);

  EXPECT_TRUE(matcher.match("exact"));
  EXPECT_FALSE(matcher.match("not"));
  EXPECT_FALSE(matcher.match(absl::nullopt));
}

} // namespace Matcher
} // namespace Envoy
