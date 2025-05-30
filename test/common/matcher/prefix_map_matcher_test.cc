#include <memory>

#include "envoy/config/core/v3/extension.pb.h"

#include "source/common/matcher/prefix_map_matcher.h"

#include "test/common/matcher/test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

TEST(PrefixMapMatcherTest, NoMatch) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      absl::nullopt);

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasNoMatch());
}

TEST(PrefixMapMatcherTest, NoMatchDueToNoData) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()}),
      absl::nullopt);

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasNoMatch());
}

TEST(PrefixMapMatcherTest, NoMatchWithFallback) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasStringAction("no_match"));
}

TEST(PrefixMapMatcherTest, Match) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasStringAction("match"));
}

TEST(PrefixMapMatcherTest, PrefixMatch) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("mat", stringOnMatch<TestData>("mat"));

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasStringAction("mat"));
}

TEST(PrefixMapMatcherTest, LongestPrefixMatch) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("mat", stringOnMatch<TestData>("mat"));
  matcher->addChild("match", stringOnMatch<TestData>("match"));
  matcher->addChild("matcher", stringOnMatch<TestData>("matcher"));

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasStringAction("match"));
}

TEST(PrefixMapMatcherTest, DataNotAvailable) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::NotAvailable, {}}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasNotEnoughData());
}

TEST(PrefixMapMatcherTest, MoreDataMightBeAvailableNoMatch) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::MoreDataMightBeAvailable, "no match"}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasNotEnoughData());
}

TEST(PrefixMapMatcherTest, MoreDataMightBeAvailableMatch) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::MoreDataMightBeAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));

  matcher->addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasStringAction("match"));
}

TEST(PrefixMapMatcherTest, MoreDataMightBeAvailableNoMatchThenMatchDoesNotPerformSecondMatch) {
  std::unique_ptr<PrefixMapMatcher<TestData>> matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::MoreDataMightBeAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));
  std::unique_ptr<PrefixMapMatcher<TestData>> child_matcher = *PrefixMapMatcher<TestData>::create(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::MoreDataMightBeAvailable, "match"}),
      absl::nullopt);

  matcher->addChild("match", {nullptr, std::move(child_matcher)});
  matcher->addChild("mat", stringOnMatch<TestData>("second_match"));

  TestData data;
  const auto result = matcher->match(data);
  EXPECT_THAT(result, HasNotEnoughData());
}

} // namespace Matcher
} // namespace Envoy
