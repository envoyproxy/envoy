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

using testing::_;

namespace {
class TestMatcher {
public:
  TestMatcher(std::string key, uint32_t numerator,
              envoy::type::v3::FractionalPercent_DenominatorType denominator, uint64_t seed)
      : key_(key) {
    default_value_.set_numerator(numerator);
    default_value_.set_denominator(denominator);

    envoy::config::core::v3::RuntimeFractionalPercent runtime_fraction;
    runtime_fraction.set_runtime_key(key);
    runtime_fraction.mutable_default_value()->CopyFrom(default_value_);

    matcher_ = std::make_unique<Matcher>(runtime_, runtime_fraction, seed);
  }

  uint64_t matchAndReturnHash(absl::optional<absl::string_view> value, bool result) {
    uint64_t called_random_value = 0;
    EXPECT_CALL(
        runtime_.snapshot_,
        featureEnabled(key_, testing::Matcher<const envoy::type::v3::FractionalPercent&>(_), _))
        .WillOnce([&](absl::string_view, const envoy::type::v3::FractionalPercent& default_value,
                      uint64_t random_value) -> bool {
          EXPECT_THAT(default_value, ProtoEq(default_value_));
          called_random_value = random_value;
          return result;
        });

    EXPECT_EQ(matcher_->match(value.has_value()
                                  ? ::Envoy::Matcher::MatchingDataType(std::string(value.value()))
                                  : absl::monostate()),
              result);
    return called_random_value;
  }

  void matchWithoutValue() {
    EXPECT_CALL(
        runtime_.snapshot_,
        featureEnabled(key_, testing::Matcher<const envoy::type::v3::FractionalPercent&>(_), _))
        .Times(0);
    EXPECT_FALSE(matcher_->match(absl::monostate()));
  }

private:
  testing::NiceMock<Runtime::MockLoader> runtime_;
  std::string key_;
  envoy::type::v3::FractionalPercent default_value_;
  std::unique_ptr<Matcher> matcher_;
};

} // namespace

class MatcherTest : public testing::Test {
protected:
  void SetUp() override {
    matcher1_ =
        std::make_unique<TestMatcher>("key1", 42, envoy::type::v3::FractionalPercent::HUNDRED, 0);
    matcher2_ = std::make_unique<TestMatcher>("key2", 21,
                                              envoy::type::v3::FractionalPercent::TEN_THOUSAND, 0);
    matcher3_ =
        std::make_unique<TestMatcher>("key3", 42, envoy::type::v3::FractionalPercent::HUNDRED, 1);
  }

  std::unique_ptr<TestMatcher> matcher1_;
  std::unique_ptr<TestMatcher> matcher2_;
  std::unique_ptr<TestMatcher> matcher3_;
};

TEST_F(MatcherTest, NoMatchOnNoInput) {
  // If there is no input, fallback to no match.
  matcher1_->matchWithoutValue();
  matcher2_->matchWithoutValue();
  matcher3_->matchWithoutValue();
}

TEST_F(MatcherTest, SameInput) {
  // Same input provides the same hash iff seed is the same.
  const auto hash1 = matcher1_->matchAndReturnHash("value1", true);
  const auto hash2 = matcher2_->matchAndReturnHash("value1", true);
  const auto hash3 = matcher3_->matchAndReturnHash("value1", true);

  EXPECT_EQ(hash1, hash2);
  EXPECT_NE(hash1, hash3);
}

TEST_F(MatcherTest, DifferentInput) {
  // Different input provides different hash.
  const auto hash1 = matcher1_->matchAndReturnHash("value1", true);
  const auto hash2 = matcher2_->matchAndReturnHash("value2", true);
  const auto hash3 = matcher3_->matchAndReturnHash("value3", true);

  EXPECT_NE(hash1, hash2);
  EXPECT_NE(hash1, hash3);
  EXPECT_NE(hash2, hash3);
}

TEST_F(MatcherTest, HonorsRuntimeValue) {
  // Matcher propagates result of runtime's `featureEnabled`.
  matcher1_->matchAndReturnHash("value1", true);
  matcher2_->matchAndReturnHash("value2", false);
}

} // namespace RuntimeFraction
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
