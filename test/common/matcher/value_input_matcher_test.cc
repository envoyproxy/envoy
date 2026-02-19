#include "envoy/matcher/matcher.h"

#include "source/common/matcher/value_input_matcher.h"

#include "test/mocks/server/server_factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

TEST(ValueInputMatcher, TestMatch) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  envoy::type::matcher::v3::StringMatcher matcher_proto;
  matcher_proto.set_exact("exact");

  StringInputMatcher matcher(matcher_proto, context);

  EXPECT_EQ(matcher.match(MatchingDataType("exact")), Matcher::MatchResult::Matched);
  EXPECT_EQ(matcher.match(MatchingDataType("not")), Matcher::MatchResult::NoMatch);
  EXPECT_EQ(matcher.match(MatchingDataType(absl::monostate())), Matcher::MatchResult::NoMatch);
}

} // namespace Matcher
} // namespace Envoy
