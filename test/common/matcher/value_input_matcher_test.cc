#include "common/matcher/value_input_matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

TEST(ValueInputMatcher, TestMatch) {
  envoy::type::matcher::v3::ValueMatcher matcher_proto;
  matcher_proto.mutable_string_match()->set_exact("exact");

  ValueInputMatcher matcher(matcher_proto);

  // TODO(snowp): Expand test as we properly support all the operations.
  EXPECT_TRUE(matcher.match("exact"));
  EXPECT_FALSE(matcher.match("not"));
}

} // namespace Matcher
} // namespace Envoy