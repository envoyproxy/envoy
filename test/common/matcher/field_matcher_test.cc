#include "envoy/matcher/matcher.h"

#include "source/common/matcher/field_matcher.h"
#include "source/common/matcher/matcher.h"

#include "test/common/matcher/test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

class FieldMatcherTest : public testing::Test {
public:
  std::vector<FieldMatcherPtr<TestData>>
  createMatchers(std::vector<std::pair<bool, DataInputGetResult::DataAvailability>> values) {
    std::vector<FieldMatcherPtr<TestData>> matchers;

    matchers.reserve(values.size());
    for (const auto& v : values) {
      matchers.emplace_back(
          SingleFieldMatcher<TestData>::create(
              std::make_unique<TestInput>(DataInputGetResult{v.second, absl::monostate()}),
              std::make_unique<BoolMatcher>(v.first))
              .value());
    }

    return matchers;
  }

  std::vector<FieldMatcherPtr<TestData>> createMatchers(std::vector<bool> values) {
    std::vector<std::pair<bool, DataInputGetResult::DataAvailability>> new_values;

    new_values.reserve(values.size());
    for (const auto v : values) {
      new_values.emplace_back(v, DataInputGetResult::DataAvailability::AllDataAvailable);
    }

    return createMatchers(new_values);
  }
};

TEST_F(FieldMatcherTest, SingleFieldMatcher) {
  EXPECT_EQ(createSingleMatcher("foo", [](auto v) { return v == "foo"; })->match(TestData()),
            FieldMatchResult::matched());
  EXPECT_EQ(createSingleMatcher("foo", [](auto v) { return v != "foo"; })->match(TestData()),
            FieldMatchResult::noMatch());
  EXPECT_EQ(createSingleMatcher(
                absl::nullopt, [](auto v) { return v == "foo"; },
                DataInputGetResult::DataAvailability::NotAvailable)
                ->match(TestData()),
            FieldMatchResult::insufficientData());
  EXPECT_EQ(createSingleMatcher(
                "fo", [](auto v) { return v == "foo"; },
                DataInputGetResult::DataAvailability::MoreDataMightBeAvailable)
                ->match(TestData()),
            FieldMatchResult::insufficientData());
  EXPECT_EQ(
      createSingleMatcher(absl::nullopt, [](auto v) { return v == "foo"; })->match(TestData()),
      FieldMatchResult::noMatch());
}

TEST_F(FieldMatcherTest, AnyMatcher) {
  EXPECT_EQ(AnyFieldMatcher<TestData>(createMatchers({true, false})).match(TestData()),
            FieldMatchResult::matched());
  EXPECT_EQ(AnyFieldMatcher<TestData>(createMatchers({true, true})).match(TestData()),
            FieldMatchResult::matched());
  EXPECT_EQ(AnyFieldMatcher<TestData>(createMatchers({false, false})).match(TestData()),
            FieldMatchResult::noMatch());
  EXPECT_EQ(AnyFieldMatcher<TestData>(
                createMatchers(
                    {std::make_pair(false,
                                    DataInputGetResult::DataAvailability::MoreDataMightBeAvailable),
                     std::make_pair(true, DataInputGetResult::DataAvailability::AllDataAvailable)}))
                .match(TestData()),
            FieldMatchResult::matched());
  EXPECT_EQ(
      AnyFieldMatcher<TestData>(
          createMatchers(
              {std::make_pair(false,
                              DataInputGetResult::DataAvailability::MoreDataMightBeAvailable),
               std::make_pair(false, DataInputGetResult::DataAvailability::AllDataAvailable)}))
          .match(TestData()),
      FieldMatchResult::insufficientData());
}

TEST_F(FieldMatcherTest, AllMatcher) {
  EXPECT_EQ(AllFieldMatcher<TestData>(createMatchers({true, false})).match(TestData()),
            FieldMatchResult::noMatch());
  EXPECT_EQ(AllFieldMatcher<TestData>(createMatchers({true, true})).match(TestData()),
            FieldMatchResult::matched());
  EXPECT_EQ(AllFieldMatcher<TestData>(createMatchers({false, false})).match(TestData()),
            FieldMatchResult::noMatch());
  EXPECT_EQ(
      AllFieldMatcher<TestData>(
          createMatchers(
              {std::make_pair(false,
                              DataInputGetResult::DataAvailability::MoreDataMightBeAvailable),
               std::make_pair(false, DataInputGetResult::DataAvailability::AllDataAvailable)}))
          .match(TestData()),
      FieldMatchResult::insufficientData());
}

TEST_F(FieldMatcherTest, NotMatcher) {
  EXPECT_EQ(NotFieldMatcher<TestData>(
                std::make_unique<AllFieldMatcher<TestData>>(createMatchers({true, false})))
                .match(TestData()),
            FieldMatchResult::matched());

  EXPECT_EQ(
      NotFieldMatcher<TestData>(
          std::make_unique<AllFieldMatcher<TestData>>(createMatchers(
              {std::make_pair(false,
                              DataInputGetResult::DataAvailability::MoreDataMightBeAvailable),
               std::make_pair(false, DataInputGetResult::DataAvailability::AllDataAvailable)})))
          .match(TestData()),
      FieldMatchResult::insufficientData());
}

} // namespace Matcher
} // namespace Envoy
