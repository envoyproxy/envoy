#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/runtime/runtime_protos.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Runtime {
namespace {

class RuntimeProtosTest : public testing::Test {
protected:
  NiceMock<MockLoader> runtime_;
};

TEST_F(RuntimeProtosTest, UInt32Test) {
  envoy::config::core::v3::RuntimeUInt32 uint32_proto;
  std::string yaml(R"EOF(
runtime_key: "foo.bar"
default_value: 99
)EOF");
  TestUtility::loadFromYamlAndValidate(yaml, uint32_proto);
  UInt32 test_uint32(uint32_proto, runtime_);

  EXPECT_EQ("foo.bar", test_uint32.runtimeKey());

  EXPECT_CALL(runtime_.snapshot_, getInteger("foo.bar", 99));
  EXPECT_EQ(99, test_uint32.value());

  EXPECT_CALL(runtime_.snapshot_, getInteger("foo.bar", 99)).WillOnce(Return(1024));
  EXPECT_EQ(1024, test_uint32.value());

  EXPECT_CALL(runtime_.snapshot_, getInteger("foo.bar", 99)).WillOnce(Return(1ull << 33));
  EXPECT_EQ(99, test_uint32.value());
}

TEST_F(RuntimeProtosTest, PercentBasicTest) {
  envoy::config::core::v3::RuntimePercent percent_proto;
  std::string yaml(R"EOF(
runtime_key: "foo.bar"
default_value:
  value: 4.2
)EOF");
  TestUtility::loadFromYamlAndValidate(yaml, percent_proto);
  Percentage test_percent(percent_proto, runtime_);

  // Basic double values and overrides.
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 4.2));
  EXPECT_EQ(4.2, test_percent.value());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 4.2)).WillOnce(Return(1.337));
  EXPECT_EQ(1.337, test_percent.value());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 4.2)).WillOnce(Return(1));
  EXPECT_EQ(1.0, test_percent.value());

  // Verify handling of bogus percentages (outside [0,100]).
  yaml = R"EOF(
runtime_key: "foo.bar"
default_value:
  value: -20
)EOF";
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, percent_proto), EnvoyException);

  yaml = R"EOF(
runtime_key: "foo.bar"
default_value:
  value: 400
)EOF";
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, percent_proto), EnvoyException);

  yaml = R"EOF(
runtime_key: "foo.bar"
default_value:
  value: 23.0
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, percent_proto);
  Percentage test_percent2(percent_proto, runtime_);
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 23.0));
  EXPECT_EQ(23.0, test_percent2.value());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 23.0)).WillOnce(Return(1.337));
  EXPECT_EQ(1.337, test_percent2.value());

  // Return default value if bogus runtime values given.
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 23.0)).WillOnce(Return(-10.0));
  EXPECT_EQ(23.0, test_percent2.value());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 23.0)).WillOnce(Return(160.0));
  EXPECT_EQ(23.0, test_percent2.value());
}

TEST_F(RuntimeProtosTest, DoubleBasicTest) {
  envoy::config::core::v3::RuntimeDouble double_proto;
  std::string yaml(R"EOF(
runtime_key: "foo.bar"
default_value: 4.2
)EOF");
  TestUtility::loadFromYamlAndValidate(yaml, double_proto);
  Double test_double(double_proto, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 4.2));
  EXPECT_EQ(4.2, test_double.value());

  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 4.2)).WillOnce(Return(1.337));
  EXPECT_EQ(1.337, test_double.value());

  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.bar", 4.2)).WillOnce(Return(1));
  EXPECT_EQ(1.0, test_double.value());
}

TEST_F(RuntimeProtosTest, FeatureFlagBasicTest) {
  envoy::config::core::v3::RuntimeFeatureFlag feature_flag_proto;
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

  envoy::config::core::v3::RuntimeFeatureFlag feature_flag_proto2;
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

TEST_F(RuntimeProtosTest, FeatureFlagEmptyProtoTest) {
  envoy::config::core::v3::RuntimeFeatureFlag empty_proto;
  FeatureFlag test(empty_proto, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("", true));
  EXPECT_EQ(true, test.enabled());
}

TEST_F(RuntimeProtosTest, FractionalPercentBasicTest) {
  envoy::config::core::v3::RuntimeFractionalPercent runtime_fractional_percent_proto;
  std::string yaml(R"EOF(
runtime_key: "foo.bar"
default_value:
  numerator: 100
  denominator: HUNDRED
)EOF");
  TestUtility::loadFromYamlAndValidate(yaml, runtime_fractional_percent_proto);
  FractionalPercent test_fractional_percent(runtime_fractional_percent_proto, runtime_);

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("foo.bar", testing::Matcher<const envoy::type::v3::FractionalPercent&>(
                                            Percent(100))))
      .WillOnce(Return(true));
  EXPECT_EQ(true, test_fractional_percent.enabled());

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("foo.bar", testing::Matcher<const envoy::type::v3::FractionalPercent&>(
                                            Percent(100))))
      .WillOnce(Return(false));
  EXPECT_EQ(false, test_fractional_percent.enabled());

  envoy::config::core::v3::RuntimeFractionalPercent runtime_fractional_percent_proto2;
  yaml = (R"EOF(
runtime_key: "foo.bar"
default_value:
  numerator: 0
  denominator: HUNDRED
)EOF");
  TestUtility::loadFromYamlAndValidate(yaml, runtime_fractional_percent_proto2);
  FractionalPercent test_fractional_percent2(runtime_fractional_percent_proto2, runtime_);

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("foo.bar", testing::Matcher<const envoy::type::v3::FractionalPercent&>(
                                            Percent(0))))
      .WillOnce(Return(true));
  EXPECT_EQ(true, test_fractional_percent2.enabled());

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("foo.bar", testing::Matcher<const envoy::type::v3::FractionalPercent&>(
                                            Percent(0))))
      .WillOnce(Return(false));
  EXPECT_EQ(false, test_fractional_percent2.enabled());
}

} // namespace
} // namespace Runtime
} // namespace Envoy
