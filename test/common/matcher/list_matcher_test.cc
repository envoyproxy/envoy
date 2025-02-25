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

TEST(ListMatcherTest, Reentry) {
  Envoy::Matcher::ListMatcher<TestData> matcher(stringOnMatch<TestData>("on no match"));

  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     stringOnMatch<TestData>("match 1"));
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return false; }),
                     stringOnMatch<TestData>("no match 1"));
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     stringOnMatch<TestData>("match 2"));
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return false; }),
                     stringOnMatch<TestData>("no match 2"));

  // Expect re-entry option to be available for indices 1-3.
  MatchTree<TestData>::MatchResult result_1 = matcher.match(TestData());
  verifyImmediateMatch(result_1, "match 1");
  ASSERT_NE(result_1.matcher_reentrant_, nullptr);

  // Expect re-entry to hit the second match & return another re-entry option for index 3.
  MatchTree<TestData>::MatchResult result_2 = result_1.matcher_reentrant_->match(TestData());
  verifyImmediateMatch(result_2, "match 2");
  ASSERT_NE(result_2.matcher_reentrant_, nullptr);

  // Expect a third match to miss index 3 and return the on_no_match action.
  MatchTree<TestData>::MatchResult result_3 = result_2.matcher_reentrant_->match(TestData());
  verifyImmediateMatch(result_3, "on no match");
  EXPECT_EQ(result_3.matcher_reentrant_, nullptr);
}

} // namespace
} // namespace Matcher
} // namespace Envoy
