#include <memory>

#include "envoy/config/core/v3/extension.pb.h"

#include "common/matcher/exact_map_matcher.h"
#include "common/matcher/matcher.h"

#include "test/common/matcher/test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

class ExactMapMatcherTest : public ::testing::Test {
public:
  void verifyNoMatch(const MatchTree<TestData>::MatchResult& result) {
    EXPECT_EQ(MatchState::MatchComplete, result.match_state_);
    EXPECT_FALSE(result.on_match_.has_value());
  }

  void verifyImmediateMatch(const MatchTree<TestData>::MatchResult& result,
                            absl::string_view expected_value) {
    EXPECT_EQ(MatchState::MatchComplete, result.match_state_);
    EXPECT_TRUE(result.on_match_.has_value());

    EXPECT_EQ(nullptr, result.on_match_->matcher_);
    EXPECT_NE(result.on_match_->action_cb_, nullptr);

    EXPECT_EQ(*static_cast<StringAction*>(result.on_match_->action_cb_().get()),
              *stringValue(expected_value));
  }

  void verifyNotEnoughDataForMatch(const MatchTree<TestData>::MatchResult& result) {
    EXPECT_EQ(MatchState::UnableToMatch, result.match_state_);
    EXPECT_FALSE(result.on_match_.has_value());
  }
};

TEST_F(ExactMapMatcherTest, NoMatch) {
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "blah"}),
      absl::nullopt);

  TestData data;
  const auto result = matcher.match(data);
  verifyNoMatch(result);
}

TEST_F(ExactMapMatcherTest, NoMatchDueToNoData) {
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt}),
      absl::nullopt);

  TestData data;
  const auto result = matcher.match(data);
  verifyNoMatch(result);
}

TEST_F(ExactMapMatcherTest, NoMatchWithFallback) {
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "blah"}),
      stringOnMatch<TestData>("no_match"));

  TestData data;
  const auto result = matcher.match(data);
  verifyImmediateMatch(result, "no_match");
}

TEST_F(ExactMapMatcherTest, Match) {
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));

  matcher.addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher.match(data);
  verifyImmediateMatch(result, "match");
}

TEST_F(ExactMapMatcherTest, DataNotAvailable) {
  ExactMapMatcher<TestData> matcher(std::make_unique<TestInput>(DataInputGetResult{
                                        DataInputGetResult::DataAvailability::NotAvailable, {}}),
                                    stringOnMatch<TestData>("no_match"));

  matcher.addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher.match(data);
  verifyNotEnoughDataForMatch(result);
}

TEST_F(ExactMapMatcherTest, MoreDataMightBeAvailableNoMatch) {
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::MoreDataMightBeAvailable, "no match"}),
      stringOnMatch<TestData>("no_match"));

  matcher.addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher.match(data);
  verifyNotEnoughDataForMatch(result);
}

TEST_F(ExactMapMatcherTest, MoreDataMightBeAvailableMatch) {
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(DataInputGetResult{
          DataInputGetResult::DataAvailability::MoreDataMightBeAvailable, "match"}),
      stringOnMatch<TestData>("no_match"));

  matcher.addChild("match", stringOnMatch<TestData>("match"));

  TestData data;
  const auto result = matcher.match(data);
  verifyImmediateMatch(result, "match");
}
} // namespace Matcher
} // namespace Envoy