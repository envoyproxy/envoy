#include <string>

#include "common/runtime/runtime_features.h"

#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
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
  FeatureFlag test_feature("foo.bar", true, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.bar", true));
  EXPECT_EQ(true, test_feature.enabled());

  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.bar", true)).WillOnce(Return(false));
  EXPECT_EQ(false, test_feature.enabled());

  FeatureFlag test_feature2("bar.foo", false, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("bar.foo", false));
  EXPECT_EQ(false, test_feature2.enabled());

  EXPECT_CALL(runtime_.snapshot_, getBoolean("bar.foo", false)).WillOnce(Return(true));
  EXPECT_EQ(true, test_feature2.enabled());
}

} // namespace
} // namespace Runtime
} // namespace Envoy
