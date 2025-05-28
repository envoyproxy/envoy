#include <memory>

#include "envoy/config/core/v3/extension.pb.h"

#include "source/common/matcher/exact_map_matcher.h"

#include "test/common/matcher/test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

using ::testing::ElementsAre;

TEST(ExactMapMatcherTest, NoMatch) {
  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "blah"}),
      absl::nullopt);

  TestData data;
  const auto result = matcher->match(data);
  verifyNoMatch(result);
}

TEST(ExactMapMatcherTest, NoMatchDueToNoData) {
  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()}),
      absl::nullopt);

  TestData data;
  const auto result = matcher->match(data);
  verifyNoMatch(result);
}

TEST(ExactMapMatcherTest, NoMatchWithFallback) {
  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "blah"}),
      stringOnMatch<TestData>("no_match"));

  TestData data;
  const auto result = matcher->match(data);
  verifyImmediateMatch(result, "no_match");
}

TEST(ExactMapMatcherTest, Match) {
  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher->match(data);
  verifyImmediateMatch(result, "match");
}

TEST(ExactMapMatcherTest, DataNotAvailable) {
  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::NotAvailable, {}}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher->match(data);
  verifyNotEnoughDataForMatch(result);
}

TEST(ExactMapMatcherTest, MoreDataMightBeAvailableNoMatch) {
  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::MoreDataMightBeAvailable, "no match"}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher->match(data);
  verifyNotEnoughDataForMatch(result);
}

TEST(ExactMapMatcherTest, MoreDataMightBeAvailableMatch) {
  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::MoreDataMightBeAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher->match(data);
  verifyImmediateMatch(result, "match");
}

TEST(ExactMapMatcherTest, RecursiveMatching) {
  auto sub_matcher = std::shared_ptr<ExactMapMatcher<TestData>>(*ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      stringOnMatch<TestData>("no_match")));
  sub_matcher->addChild("match", stringOnMatch<TestData>("match"));

  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));
  matcher->addChild("match", OnMatch<TestData>{/*.action_cb=*/nullptr, /*.matcher=*/sub_matcher,
                                               /*.keep_matching=*/false});

  TestData data;
  const auto result = matcher->match(data);
  verifyImmediateMatch(result, "match");
}

TEST(ExactMapMatcherTest, RecursiveMatchingOnNoMatch) {
  auto sub_matcher = std::shared_ptr<ExactMapMatcher<TestData>>(*ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      stringOnMatch<TestData>("nested_no_match")));
  sub_matcher->addChild("match", stringOnMatch<TestData>("nested_match"));

  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "blah"}),
      OnMatch<TestData>{/*.action_cb=*/nullptr, /*.matcher=*/sub_matcher,
                        /*.keep_matching=*/false});
  matcher->addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher->match(data);
  verifyImmediateMatch(result, "nested_match");
}

TEST(ExactMapMatcherTest, RecursiveMatchingWithKeepMatching) {
  // Match is skipped by nested keep_matching and on_no_match is skipped by top-level keep_matching.
  auto sub_matcher_match_keeps_matching =
      std::shared_ptr<ExactMapMatcher<TestData>>(*ExactMapMatcher<TestData>::create(
          std::make_unique<TestInput>(
              DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
          stringOnMatch<TestData>("nested_on_no_match_1")));
  sub_matcher_match_keeps_matching->addChild(
      "match", stringOnMatch<TestData>("nested_match_1", /*keep_matching=*/true));

  // Recursive on_no_match should still work.
  auto top_on_no_match_matcher =
      std::shared_ptr<ExactMapMatcher<TestData>>(*ExactMapMatcher<TestData>::create(
          std::make_unique<TestInput>(
              DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
          stringOnMatch<TestData>("top_level_no_match")));

  std::unique_ptr<ExactMapMatcher<TestData>> matcher = *ExactMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      OnMatch<TestData>{/*.action_cb=*/nullptr, /*.matcher=*/top_on_no_match_matcher,
                        /*.keep_matching=*/false});
  matcher->addChild("match", OnMatch<TestData>{/*.action_cb=*/nullptr,
                                               /*.matcher=*/sub_matcher_match_keeps_matching,
                                               /*.keep_matching=*/true});

  std::vector<ActionFactoryCb> skipped_results{};
  SkippedMatchCb<TestData> skipped_match_cb = [&skipped_results](const OnMatch<TestData>& match) {
    skipped_results.push_back(match.action_cb_);
  };
  TestData data;
  const auto result = matcher->match(data, skipped_match_cb);
  EXPECT_THAT(result, HasStringAction("top_level_no_match"));
  EXPECT_THAT(skipped_results, ElementsAre(IsStringAction("nested_match_1"),
                                           IsStringAction("nested_on_no_match_1")));
}

} // namespace Matcher
} // namespace Envoy
