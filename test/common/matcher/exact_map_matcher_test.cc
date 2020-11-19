#include <memory>

#include "envoy/config/core/v3/extension.pb.h"

#include "common/matcher/exact_map_matcher.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

struct TestData {};

struct StringAction : public Action {
  explicit StringAction(const std::string& string) : string_(string) {}

  std::string string_;

  bool operator==(const StringAction& other) const { return string_ == other.string_; }
};

struct TestInput : public DataInput<TestData> {
  explicit TestInput(DataInputGetResult result) : result_(result) {}
  DataInputGetResult get(const TestData&) { return result_; }

  DataInputGetResult result_;
};

class ExactMapMatcherTest : public ::testing::Test {
public:
  std::unique_ptr<StringAction> stringValue(absl::string_view value) {
    return std::make_unique<StringAction>(std::string(value));
  }

  void verifyNoMatch(const MatchTree<TestData>::MatchResult& result) {
    EXPECT_TRUE(result.match_completed_);
    EXPECT_FALSE(result.on_match_.has_value());
  }

  void verifyImmediateMatch(const MatchTree<TestData>::MatchResult& result,
                            absl::string_view expected_value) {
    EXPECT_TRUE(result.match_completed_);
    EXPECT_TRUE(result.on_match_.has_value());

    EXPECT_EQ(nullptr, result.on_match_->matcher_);
    EXPECT_NE(result.on_match_->action_cb_, nullptr);

    EXPECT_EQ(*static_cast<StringAction*>(result.on_match_->action_cb_().get()),
              *stringValue(expected_value));
  }

  void verifyNotEnoughDataForMatch(const MatchTree<TestData>::MatchResult& result) {
    EXPECT_FALSE(result.match_completed_);
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

TEST_F(ExactMapMatcherTest, NoMatchWithFallback) {
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "blah"}),
      OnMatch<TestData>{[this]() { return stringValue("no_match"); }, nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyImmediateMatch(result, "no_match");
}

TEST_F(ExactMapMatcherTest, Match) {
  const auto no_match_config = stringValue("no_match");
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      OnMatch<TestData>{[this]() { return stringValue("no_match"); }, nullptr});

  matcher.addChild("match", OnMatch<TestData>{[this]() { return stringValue("match"); }, nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyImmediateMatch(result, "match");
}

TEST_F(ExactMapMatcherTest, DataNotAvailable) {
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::NotAvailable, {}}),
      OnMatch<TestData>{[this]() { return stringValue("no_match"); }, nullptr});

  matcher.addChild("match", OnMatch<TestData>{[this]() { return stringValue("match"); }, nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyNotEnoughDataForMatch(result);
}

TEST_F(ExactMapMatcherTest, MoreDataAvailableNoMatch) {
  const auto no_match_config = stringValue("no_match");

  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::MoreDataAvailable, "no match"}),
      OnMatch<TestData>{[this]() { return stringValue("no_match"); }, nullptr});

  matcher.addChild("match", OnMatch<TestData>{[this]() { return stringValue("match"); }, nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyNotEnoughDataForMatch(result);
}

TEST_F(ExactMapMatcherTest, MoreDataAvailableMatch) {
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::MoreDataAvailable, "match"}),
      OnMatch<TestData>{[this]() { return stringValue("no_match"); }, nullptr});

  matcher.addChild("match", OnMatch<TestData>{[this]() { return stringValue("match"); }, nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyImmediateMatch(result, "match");
}
} // namespace Matcher
} // namespace Envoy