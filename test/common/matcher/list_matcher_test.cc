#include "envoy/matcher/matcher.h"

#include "source/common/matcher/list_matcher.h"

#include "test/common/matcher/test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {
namespace {

TEST(ListMatcherTest, BasicUsage) {
  ListMatcher<TestData> matcher(absl::nullopt);

  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     stringOnMatch<TestData>("match"));

  EXPECT_TRUE(matcher.match(TestData()).on_match_.has_value());
  EXPECT_EQ(matcher.match(TestData()).match_state_, MatchState::MatchComplete);
}

TEST(ListMatcherTest, MissingData) {
  ListMatcher<TestData> matcher(absl::nullopt);

  matcher.addMatcher(
      createSingleMatcher(
          "string", [](auto) { return true; }, DataInputGetResult::DataAvailability::NotAvailable),
      stringOnMatch<TestData>("match"));

  EXPECT_FALSE(matcher.match(TestData()).on_match_.has_value());
  EXPECT_EQ(matcher.match(TestData()).match_state_, MatchState::UnableToMatch);
}
} // namespace
} // namespace Matcher
} // namespace Envoy
