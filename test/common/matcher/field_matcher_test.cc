#include "envoy/matcher/matcher.h"

#include "common/matcher/field_matcher.h"
#include "common/matcher/matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

struct TestData {
  static absl::string_view name() { return "test"; }
};

class FieldMatcherTest : public testing::Test {
public:
  SingleFieldMatcherPtr<TestData>
  createSingleMatcher(absl::optional<std::string> input,
                      std::function<bool(absl::optional<absl::string_view>)> predicate,
                      DataInputGetResult::DataAvailability availability =
                          DataInputGetResult::DataAvailability::AllDataAvailable) {
    return std::make_unique<SingleFieldMatcher<TestData>>(
        std::make_unique<TestInput>(input, availability), std::make_unique<TestMatcher>(predicate));
  }

  std::vector<FieldMatcherPtr<TestData>>
  createMatchers(std::vector<std::pair<bool, DataInputGetResult::DataAvailability>> values) {
    std::vector<FieldMatcherPtr<TestData>> matchers;

    matchers.reserve(values.size());
    for (const auto& v : values) {
      matchers.emplace_back(std::make_unique<SingleFieldMatcher<TestData>>(
          std::make_unique<TestInput>(absl::nullopt, v.second),
          std::make_unique<BoolMatcher>(v.first)));
    }

    return matchers;
  }

  std::vector<FieldMatcherPtr<TestData>> createMatchers(std::vector<bool> values) {
    std::vector<std::pair<bool, DataInputGetResult::DataAvailability>> new_values;

    new_values.reserve(values);
    for (const auto v : values) {
      new_values.emplace_back(v, DataInputGetResult::DataAvailability::AllDataAvailable);
    }

    return createMatchers(new_values);
  }

  struct TestInput : public DataInput<TestData> {
    TestInput(absl::optional<std::string> input, DataInputGetResult::DataAvailability availability)
        : input_(input), availability_(availability) {}

    DataInputGetResult get(const TestData&) override {
      return {availability_,
              input_ ? absl::make_optional(absl::string_view(*input_)) : absl::nullopt};
    }

    absl::optional<std::string> input_;
    DataInputGetResult::DataAvailability availability_;
  };

  struct BoolMatcher : public InputMatcher {
    explicit BoolMatcher(bool value) : value_(value) {}

    bool match(absl::optional<absl::string_view>) override { return value_; }

    const bool value_;
  };

  struct TestMatcher : public InputMatcher {
    explicit TestMatcher(std::function<bool(absl::optional<absl::string_view>)> predicate)
        : predicate_(predicate) {}

    bool match(absl::optional<absl::string_view> input) override { return predicate_(input); }

    std::function<bool(absl::optional<absl::string_view>)> predicate_;
  };
};

TEST_F(FieldMatcherTest, SingleFieldMatcher) {
  EXPECT_EQ(
      createSingleMatcher("foo", [](auto v) { return v == "foo"; })->match(TestData()).match_state_,
      MatchState::MatchComplete);
  EXPECT_EQ(createSingleMatcher(
                absl::nullopt, [](auto v) { return v == "foo"; },
                DataInputGetResult::DataAvailability::NotAvailable)
                ->match(TestData())
                .match_state_,
            MatchState::UnableToMatch);
  EXPECT_EQ(createSingleMatcher(
                "fo", [](auto v) { return v == "foo"; },
                DataInputGetResult::DataAvailability::MoreDataMightBeAvailable)
                ->match(TestData())
                .match_state_,
            MatchState::UnableToMatch);
  EXPECT_TRUE(
      createSingleMatcher("foo", [](auto v) { return v == "foo"; })->match(TestData()).result());
  EXPECT_FALSE(
      createSingleMatcher("foo", [](auto v) { return v != "foo"; })->match(TestData()).result());
  EXPECT_TRUE(createSingleMatcher(absl::nullopt, [](auto v) { return v == absl::nullopt; })
                  ->match(TestData())
                  .result());
  EXPECT_FALSE(createSingleMatcher(absl::nullopt, [](auto v) { return v == "foo"; })
                   ->match(TestData())
                   .result());
}

TEST_F(FieldMatcherTest, AnyMatcher) {
  EXPECT_TRUE(AnyFieldMatcher<TestData>(createMatchers({true, false})).match(TestData()).result());
  EXPECT_TRUE(AnyFieldMatcher<TestData>(createMatchers({true, true})).match(TestData()).result());
  EXPECT_FALSE(
      AnyFieldMatcher<TestData>(createMatchers({false, false})).match(TestData()).result());
  EXPECT_EQ(AnyFieldMatcher<TestData>(
                createMatchers(
                    {std::make_pair(false,
                                    DataInputGetResult::DataAvailability::MoreDataMightBeAvailable),
                     std::make_pair(true, DataInputGetResult::DataAvailability::AllDataAvailable)}))
                .match(TestData())
                .match_state_,
            MatchState::MatchComplete);
  EXPECT_EQ(
      AnyFieldMatcher<TestData>(
          createMatchers(
              {std::make_pair(false,
                              DataInputGetResult::DataAvailability::MoreDataMightBeAvailable),
               std::make_pair(false, DataInputGetResult::DataAvailability::AllDataAvailable)}))
          .match(TestData())
          .match_state_,
      MatchState::UnableToMatch);
}

TEST_F(FieldMatcherTest, AllMatcher) {
  EXPECT_FALSE(AllFieldMatcher<TestData>(createMatchers({true, false})).match(TestData()).result());
  EXPECT_TRUE(AllFieldMatcher<TestData>(createMatchers({true, true})).match(TestData()).result());
  EXPECT_FALSE(
      AllFieldMatcher<TestData>(createMatchers({false, false})).match(TestData()).result());
  EXPECT_EQ(
      AllFieldMatcher<TestData>(
          createMatchers(
              {std::make_pair(false,
                              DataInputGetResult::DataAvailability::MoreDataMightBeAvailable),
               std::make_pair(false, DataInputGetResult::DataAvailability::AllDataAvailable)}))
          .match(TestData())
          .match_state_,
      MatchState::UnableToMatch);
}

} // namespace Matcher
} // namespace Envoy