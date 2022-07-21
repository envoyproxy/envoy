#include "source/common/matcher/value_input_matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

TEST(ValueInputMatcher, TestMatch) {
  envoy::type::matcher::v3::StringMatcher matcher_proto;
  matcher_proto.set_exact("exact");

  StringInputMatcher matcher(matcher_proto);

  EXPECT_TRUE(matcher.match(InputValue("exact")));
  EXPECT_FALSE(matcher.match(InputValue("not")));
  EXPECT_FALSE(matcher.match(InputValue(42)));
  EXPECT_FALSE(matcher.match(InputValue()));
}

TEST(ValueInputMatcher, TestMatchNonString) {
  envoy::type::matcher::v3::StringMatcher matcher_proto;
  matcher_proto.set_exact("42");

  StringInputMatcher matcher(matcher_proto);

  EXPECT_TRUE(matcher.match(InputValue("42")));
  EXPECT_TRUE(matcher.match(InputValue(42)));
  EXPECT_FALSE(matcher.match(InputValue()));
  EXPECT_FALSE(matcher.match(InputValue(std::vector<InputValue>{InputValue(42)})));
  EXPECT_FALSE(matcher.match(InputValue(std::vector<InputValue>{InputValue("42")})));
}

} // namespace Matcher
} // namespace Envoy
