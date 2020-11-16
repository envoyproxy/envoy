#include "common/matcher/field_matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

struct TestData {};
class FieldMatcherTest : public testing::Test {
public:
  SingleFieldMatcherPtr<TestData>
  createSingleMatcher(absl::optional<std::string> input,
                      std::function<bool(absl::optional<absl::string_view>)> predicate) {
    return std::make_unique<SingleFieldMatcher<TestData>>(std::make_unique<TestInput>(input),
                                                          std::make_unique<TestMatcher>(predicate));
  }

  std::vector<FieldMatcherPtr<TestData>> createMatchers(std::vector<bool> values) {
    std::vector<FieldMatcherPtr<TestData>> matchers;

    for (const auto v : values) {
      matchers.emplace_back(std::make_unique<SingleFieldMatcher<TestData>>(
          std::make_unique<TestInput>(absl::nullopt), std::make_unique<BoolMatcher>(v)));
    }

    return matchers;
  }

  struct TestInput : public DataInput<TestData> {
    explicit TestInput(absl::optional<std::string> input) : input_(input) {}

    DataInputGetResult get(const TestData&) override {
      return {false, false,
              input_ ? absl::make_optional(absl::string_view(*input_)) : absl::nullopt};
    }

    absl::optional<std::string> input_;
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
  EXPECT_TRUE(createSingleMatcher("foo", [](auto v) { return v == "foo"; })->match(TestData()));
  EXPECT_TRUE(*createSingleMatcher("foo", [](auto v) { return v == "foo"; })->match(TestData()));
  EXPECT_FALSE(*createSingleMatcher("foo", [](auto v) { return v != "foo"; })->match(TestData()));
  EXPECT_TRUE(*createSingleMatcher(absl::nullopt, [](auto v) {
                 return v == absl::nullopt;
               })->match(TestData()));
  EXPECT_FALSE(
      *createSingleMatcher(absl::nullopt, [](auto v) { return v == "foo"; })->match(TestData()));
}

TEST_F(FieldMatcherTest, AnyMatcher) {
  EXPECT_TRUE(*AnyFieldMatcher<TestData>(createMatchers({true, false})).match(TestData()));
  EXPECT_TRUE(*AnyFieldMatcher<TestData>(createMatchers({true, true})).match(TestData()));
  EXPECT_FALSE(*AnyFieldMatcher<TestData>(createMatchers({false, false})).match(TestData()));
}

TEST_F(FieldMatcherTest, AllMatcher) {
  EXPECT_FALSE(*AllFieldMatcher<TestData>(createMatchers({true, false})).match(TestData()));
  EXPECT_TRUE(*AllFieldMatcher<TestData>(createMatchers({true, true})).match(TestData()));
  EXPECT_FALSE(*AllFieldMatcher<TestData>(createMatchers({false, false})).match(TestData()));
}

} // namespace Matcher
} // namespace Envoy