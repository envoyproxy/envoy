#include "envoy/config/core/v3/extension.pb.h"

#include "common/matcher/exact_map_matcher.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

struct TestData {};
struct TestInput : public DataInput<TestData> {
  explicit TestInput(DataInputGetResult result) : result_(result) {}
  DataInputGetResult get(const TestData&) { return result_; }

  DataInputGetResult result_;
};

class ExactMapMatcherTest : public ::testing::Test {
public:
  envoy::config::core::v3::TypedExtensionConfig stringValue(absl::string_view value) {
    ProtobufWkt::StringValue string_value;
    string_value.set_value(std::string(value));

    envoy::config::core::v3::TypedExtensionConfig config;
    config.mutable_typed_config()->PackFrom(string_value);

    return config;
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
    EXPECT_TRUE(result.on_match_->action_.has_value());

    EXPECT_TRUE(TestUtility::protoEqual(*result.on_match_->action_, stringValue(expected_value)));
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
  const auto no_match_config = stringValue("no_match");
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "blah"}),
      OnMatch<TestData>{no_match_config, nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyImmediateMatch(result, "no_match");
}

TEST_F(ExactMapMatcherTest, Match) {
  const auto no_match_config = stringValue("no_match");
  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, "match"}),
      OnMatch<TestData>{no_match_config, nullptr});

  matcher.addChild("match", OnMatch<TestData>{stringValue("match"), nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyImmediateMatch(result, "match");
}

TEST_F(ExactMapMatcherTest, DataNotAvailable) {
  const auto no_match_config = stringValue("no_match");

  ExactMapMatcher<TestData> matcher(std::make_unique<TestInput>(DataInputGetResult{
                                        DataInputGetResult::DataAvailability::NotAvailable, {}}),
                                    OnMatch<TestData>{no_match_config, nullptr});

  matcher.addChild("match", OnMatch<TestData>{stringValue("match"), nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyNotEnoughDataForMatch(result);
}

TEST_F(ExactMapMatcherTest, MoreDataAvailableNoMatch) {
  const auto no_match_config = stringValue("no_match");

  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::MoreDataAvailable, "no match"}),
      OnMatch<TestData>{no_match_config, nullptr});

  matcher.addChild("match", OnMatch<TestData>{stringValue("match"), nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyNotEnoughDataForMatch(result);
}

TEST_F(ExactMapMatcherTest, MoreDataAvailableMatch) {
  const auto no_match_config = stringValue("no_match");

  ExactMapMatcher<TestData> matcher(
      std::make_unique<TestInput>(
          DataInputGetResult{DataInputGetResult::DataAvailability::MoreDataAvailable, "match"}),
      OnMatch<TestData>{no_match_config, nullptr});

  matcher.addChild("match", OnMatch<TestData>{stringValue("match"), nullptr});

  TestData data;
  const auto result = matcher.match(data);
  verifyImmediateMatch(result, "match");
}
} // namespace Matcher
} // namespace Envoy