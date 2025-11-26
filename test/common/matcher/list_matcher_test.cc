#include "envoy/matcher/matcher.h"

#include "source/common/matcher/list_matcher.h"

#include "test/common/matcher/test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {
namespace {

using ::testing::ElementsAre;

TEST(ListMatcherTest, BasicUsage) {
  ListMatcher<TestData> matcher(absl::nullopt);

  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     stringOnMatch<TestData>("match"));

  EXPECT_THAT(matcher.match(TestData()), HasStringAction("match"));
}

TEST(ListMatcherTest, MissingData) {
  ListMatcher<TestData> matcher(absl::nullopt);

  matcher.addMatcher(
      createSingleMatcher(
          "string", [](auto) { return true; }, DataInputGetResult::DataAvailability::NotAvailable),
      stringOnMatch<TestData>("match"));

  EXPECT_THAT(matcher.match(TestData()), HasInsufficientData());
}

TEST(ListMatcherTest, KeepMatching) {
  // Expect a no-match return due to keep_matching = true.
  Envoy::Matcher::ListMatcher<TestData> matcher(absl::nullopt);
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     stringOnMatch<TestData>("keep matching", /*keep_matching=*/true));
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     stringOnMatch<TestData>("matched", /*keep_matching=*/false));

  std::vector<ActionConstSharedPtr> skipped_results;
  SkippedMatchCb skipped_match_cb = [&skipped_results](ActionConstSharedPtr cb) {
    skipped_results.push_back(cb);
  };
  auto result = matcher.match(TestData(), skipped_match_cb);
  EXPECT_THAT(result, HasStringAction("matched"));
  EXPECT_THAT(skipped_results, ElementsAre(IsStringAction("keep matching")));
}

TEST(ListMatcherTest, KeepMatchingOnNoMatch) {
  Envoy::Matcher::ListMatcher<TestData> matcher(stringOnMatch<TestData>("on no match"));
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     stringOnMatch<TestData>("keep matching 1", /*keep_matching=*/true));
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     stringOnMatch<TestData>("keep matching 2", /*keep_matching=*/true));

  std::vector<ActionConstSharedPtr> skipped_results;
  SkippedMatchCb skipped_match_cb = [&skipped_results](const ActionConstSharedPtr cb) {
    skipped_results.push_back(cb);
  };
  auto result = matcher.match(TestData(), skipped_match_cb);
  EXPECT_THAT(result, HasStringAction("on no match"));
  EXPECT_THAT(skipped_results,
              ElementsAre(IsStringAction("keep matching 1"), IsStringAction("keep matching 2")));
}

TEST(ListMatcherTest, KeepMatchingWithRecursion) {
  // First sub-matcher should return a keep_matching match.
  auto sub_matcher_1 = std::make_shared<Envoy::Matcher::ListMatcher<TestData>>(absl::nullopt);
  sub_matcher_1->addMatcher(
      createSingleMatcher("string", [](auto) { return true; }),
      stringOnMatch<TestData>("sub match keep_matching", /*keep_matching=*/true));
  // Second sub-matcher returns a matching action but the top-level matchers will be set with
  // keep_matching.
  auto sub_matcher_2 = std::make_shared<Envoy::Matcher::ListMatcher<TestData>>(absl::nullopt);
  sub_matcher_2->addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                            stringOnMatch<TestData>("match 1", /*keep_matching=*/false));
  // Third sub-matcher returns a match that will be actually returned.
  auto sub_matcher_3 = std::make_shared<Envoy::Matcher::ListMatcher<TestData>>(absl::nullopt);
  sub_matcher_3->addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                            stringOnMatch<TestData>("match 2", /*keep_matching=*/false));

  Envoy::Matcher::ListMatcher<TestData> matcher(stringOnMatch<TestData>("top_level on_no_match"));
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     OnMatch<TestData>{/*.action_=*/nullptr, /*.matcher=*/sub_matcher_1,
                                       /*.keep_matching=*/false});
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     OnMatch<TestData>{/*.action_=*/nullptr, /*.matcher=*/sub_matcher_2,
                                       /*.keep_matching=*/true});
  matcher.addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                     OnMatch<TestData>{/*.action_=*/nullptr, /*.matcher=*/sub_matcher_3,
                                       /*.keep_matching=*/false});

  std::vector<ActionConstSharedPtr> skipped_results;
  SkippedMatchCb skipped_match_cb = [&skipped_results](ActionConstSharedPtr cb) {
    skipped_results.push_back(cb);
  };
  MatchResult result = matcher.match(TestData(), skipped_match_cb);
  EXPECT_THAT(result, HasStringAction("match 2"));
  EXPECT_THAT(skipped_results,
              ElementsAre(IsStringAction("sub match keep_matching"), IsStringAction("match 1")));
}

TEST(ListMatcherTest, KeepMatchingWithRecursiveOnNoMatch) {
  // Expect a no-match return due to keep_matching = true.
  auto sub_matcher_1 = std::make_shared<Envoy::Matcher::ListMatcher<TestData>>(
      stringOnMatch<TestData>("sub on_no_match keep_matching", /*keep_matching=*/false));
  sub_matcher_1->addMatcher(
      createSingleMatcher("string", [](auto) { return true; }),
      stringOnMatch<TestData>("sub match keep_matching", /*keep_matching=*/true));
  auto sub_matcher_2 = std::make_shared<Envoy::Matcher::ListMatcher<TestData>>(absl::nullopt);
  sub_matcher_2->addMatcher(createSingleMatcher("string", [](auto) { return false; }),
                            stringOnMatch<TestData>("no match", /*keep_matching=*/false));
  auto on_no_match_sub_matcher = std::make_shared<Envoy::Matcher::ListMatcher<TestData>>(
      stringOnMatch<TestData>("on_no_match sub on_no_match", /*keep_matching=*/false));
  on_no_match_sub_matcher->addMatcher(
      createSingleMatcher("string", [](auto) { return true; }),
      stringOnMatch<TestData>("on_no_match sub match", /*keep_matching=*/true));

  Envoy::Matcher::ListMatcher<TestData> matcher(
      OnMatch<TestData>{/*action_=*/nullptr,
                        /*matcher=*/on_no_match_sub_matcher, /*keep_matching=*/false});
  matcher.addMatcher(
      createSingleMatcher("string", [](auto) { return true; }),
      OnMatch<TestData>{/*action_=*/nullptr, /*matcher=*/sub_matcher_1, /*keep_matching=*/true});
  matcher.addMatcher(
      createSingleMatcher("string", [](auto) { return true; }),
      OnMatch<TestData>{/*action_=*/nullptr, /*matcher=*/sub_matcher_2, /*keep_matching=*/false});

  std::vector<ActionConstSharedPtr> skipped_results;
  SkippedMatchCb skipped_match_cb = [&skipped_results](ActionConstSharedPtr cb) {
    skipped_results.push_back(cb);
  };
  MatchResult result = matcher.match(TestData(), skipped_match_cb);
  EXPECT_THAT(result, HasStringAction("on_no_match sub on_no_match"));
  EXPECT_THAT(skipped_results, ElementsAre(IsStringAction("sub match keep_matching"),
                                           IsStringAction("sub on_no_match keep_matching"),
                                           IsStringAction("on_no_match sub match")));
}

} // namespace
} // namespace Matcher
} // namespace Envoy
