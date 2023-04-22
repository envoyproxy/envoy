#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/extensions/matching/input_matchers/runtime_fraction/matcher.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace RuntimeFraction {

namespace {

class TestMatcher {
public:
  TestMatcher(std::string key, uint32_t numerator,
              envoy::type::v3::FractionalPercent_DenominatorType denumerator, uint64_t seed)
      : key_(key) {
    default_value_.set_numerator(numerator);
    default_value_.set_denominator(denumerator);

    envoy::config::core::v3::RuntimeFractionalPercent runtime_fraction;
    runtime_fraction.set_runtime_key(key);
    runtime_fraction.mutable_default_value()->CopyFrom(default_value_);

    matcher_ = std::make_unique<Matcher>(runtime_, runtime_fraction, seed);
  }

  uint64_t expectCall(absl::optional<absl::string_view> value, bool result) {
    envoy::type::v3::FractionalPercent called_default_value;
    uint64_t called_random_value;
    EXPECT_CALL(runtime_.snapshot_,
                featureEnabled(
                    key_, testing::Matcher<const envoy::type::v3::FractionalPercent&>(testing::_),
                    testing::Matcher<uint64_t>(testing::_)))
        .WillOnce(testing::DoAll(testing::SaveArg<1>(&called_default_value),
                                 testing::SaveArg<2>(&called_random_value),
                                 testing::Return(result)));
    EXPECT_EQ(matcher_->match(value), result);
    EXPECT_THAT(called_default_value, ProtoEq(default_value_));
    return called_random_value;
  }

  void expectCallWithoutValue() {
    EXPECT_CALL(runtime_.snapshot_,
                featureEnabled(
                    key_, testing::Matcher<const envoy::type::v3::FractionalPercent&>(testing::_),
                    testing::Matcher<uint64_t>(testing::_)))
        .Times(0);
    EXPECT_FALSE(matcher_->match(absl::nullopt));
  }

private:
  testing::NiceMock<Runtime::MockLoader> runtime_;
  std::string key_;
  envoy::type::v3::FractionalPercent default_value_;
  std::unique_ptr<Matcher> matcher_;
};

} // namespace

// Validates that independent matchers agree on the match result for various inputs.
TEST(MatcherTest, BasicUsage) {
  TestMatcher matcher1("key1", 42, envoy::type::v3::FractionalPercent::HUNDRED, 0);
  TestMatcher matcher2("key2", 21, envoy::type::v3::FractionalPercent::TEN_THOUSAND, 0);
  TestMatcher matcher3("key3", 42, envoy::type::v3::FractionalPercent::HUNDRED, 1);

  {
    // If there is no input, fallback to no match.
    matcher1.expectCallWithoutValue();
    matcher2.expectCallWithoutValue();
    matcher3.expectCallWithoutValue();
  }

  {
    // Same input provides the same hash iff seed is the same.
    const auto hash1 = matcher1.expectCall("value1", true);
    const auto hash2 = matcher2.expectCall("value1", true);
    const auto hash3 = matcher3.expectCall("value1", true);

    EXPECT_EQ(hash1, hash2);
    EXPECT_NE(hash1, hash3);
  }
  {
    // Different input provides different hash.
    const auto hash1 = matcher1.expectCall("value1", false);
    const auto hash2 = matcher2.expectCall("value2", false);
    const auto hash3 = matcher3.expectCall("value3", false);

    EXPECT_NE(hash1, hash2);
    EXPECT_NE(hash1, hash3);
  }
}

} // namespace RuntimeFraction
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
