#include <string>

#include "envoy/api/v2/core/base.pb.h"

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

TEST_F(FeatureFlagTest, BasicTest) {
  auto feature_flag_proto = TestUtility::parseYaml<envoy::api::v2::core::RuntimeFeatureFlag>(R"EOF(
runtime_key: "foo.bar"
default_value: true
)EOF");
  FeatureFlag test_feature(feature_flag_proto, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.bar", true));
  EXPECT_EQ(true, test_feature.enabled());

  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.bar", true)).WillOnce(Return(false));
  EXPECT_EQ(false, test_feature.enabled());

  auto feature_flag_proto2 = TestUtility::parseYaml<envoy::api::v2::core::RuntimeFeatureFlag>(R"EOF(
runtime_key: "bar.foo"
default_value: false
)EOF");
  FeatureFlag test_feature2(feature_flag_proto2, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("bar.foo", false));
  EXPECT_EQ(false, test_feature2.enabled());

  EXPECT_CALL(runtime_.snapshot_, getBoolean("bar.foo", false)).WillOnce(Return(true));
  EXPECT_EQ(true, test_feature2.enabled());
}

} // namespace
} // namespace Runtime
} // namespace Envoy
