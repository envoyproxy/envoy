#include <memory>

#include "envoy/config/core/v3/extension.pb.h"

#include "source/common/matcher/exact_map_matcher.h"

#include "test/common/matcher/test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

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
} // namespace Matcher
} // namespace Envoy
