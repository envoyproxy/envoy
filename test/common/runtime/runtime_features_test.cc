#include <string>

#include "envoy/api/v2/core/base.pb.validate.h"

#include "common/runtime/runtime_features.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Runtime {
namespace {

class FeatureFlagTest : public testing::Test {
protected:
  NiceMock<MockLoader> runtime_;
};

TEST_F(FeatureFlagTest, FeatureFlagBasicTest) {
  envoy::api::v2::core::RuntimeFeatureFlag feature_flag_proto;
  std::string yaml(R"EOF(
runtime_key: "foo.bar"
default_value: true
)EOF");
  TestUtility::loadFromYamlAndValidate(yaml, feature_flag_proto);
  FeatureFlag test_feature(feature_flag_proto, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.bar", true));
  EXPECT_EQ(true, test_feature.enabled());

  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.bar", true)).WillOnce(Return(false));
  EXPECT_EQ(false, test_feature.enabled());

  envoy::api::v2::core::RuntimeFeatureFlag feature_flag_proto2;
  yaml = R"EOF(
runtime_key: "bar.foo"
default_value: false
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, feature_flag_proto2);
  FeatureFlag test_feature2(feature_flag_proto2, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("bar.foo", false));
  EXPECT_EQ(false, test_feature2.enabled());

  EXPECT_CALL(runtime_.snapshot_, getBoolean("bar.foo", false)).WillOnce(Return(true));
  EXPECT_EQ(true, test_feature2.enabled());
}

TEST_F(FeatureFlagTest, FeatureFlagEmptyProtoTest) {
  envoy::api::v2::core::RuntimeFeatureFlag empty_proto;
  FeatureFlag test(empty_proto, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("", true));
  EXPECT_EQ(true, test.enabled());
}

TEST_F(FeatureFlagTest, FractionalPercentBasicTest) {
  envoy::api::v2::core::RuntimeFractionalPercent runtime_fractional_percent_proto;
  std::string yaml(R"EOF(
runtime_key: "foo.bar"
default_value:
  numerator: 100
  denominator: HUNDRED
)EOF");
  TestUtility::loadFromYamlAndValidate(yaml, runtime_fractional_percent_proto);
  FractionalPercent test_fractional_percent(runtime_fractional_percent_proto, runtime_);

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("foo.bar",
                             testing::Matcher<const envoy::type::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));
  EXPECT_EQ(true, test_fractional_percent.enabled());

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("foo.bar",
                             testing::Matcher<const envoy::type::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(false));
  EXPECT_EQ(false, test_fractional_percent.enabled());

  envoy::api::v2::core::RuntimeFractionalPercent runtime_fractional_percent_proto2;
  yaml = (R"EOF(
runtime_key: "foo.bar"
default_value:
  numerator: 0
  denominator: HUNDRED
)EOF");
  TestUtility::loadFromYamlAndValidate(yaml, runtime_fractional_percent_proto2);
  FractionalPercent test_fractional_percent2(runtime_fractional_percent_proto2, runtime_);

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("foo.bar",
                             testing::Matcher<const envoy::type::FractionalPercent&>(Percent(0))))
      .WillOnce(Return(true));
  EXPECT_EQ(true, test_fractional_percent2.enabled());

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("foo.bar",
                             testing::Matcher<const envoy::type::FractionalPercent&>(Percent(0))))
      .WillOnce(Return(false));
  EXPECT_EQ(false, test_fractional_percent2.enabled());
}

} // namespace
} // namespace Runtime
} // namespace Envoy
