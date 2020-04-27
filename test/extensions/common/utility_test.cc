#include "extensions/common/utility.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Utility {
namespace {

// Test that deprecated names indicate warning or block depending on runtime flags.
TEST(ExtensionNameUtilTest, DEPRECATED_FEATURE_TEST(TestDeprecatedExtensionNameStatus)) {
  // Validate that no runtime available results in warnings.
  {
    EXPECT_EQ(ExtensionNameUtil::Status::Warn,
              ExtensionNameUtil::deprecatedExtensionNameStatus(nullptr));
  }

  // If deprecated feature is enabled, warn.
  {
    NiceMock<Runtime::MockLoader> runtime;

    EXPECT_CALL(
        runtime.snapshot_,
        deprecatedFeatureEnabled("envoy.deprecated_features.allow_deprecated_extension_names", _))
        .WillRepeatedly(Return(true));

    EXPECT_EQ(ExtensionNameUtil::Status::Warn,
              ExtensionNameUtil::deprecatedExtensionNameStatus(&runtime));
  }

  // If deprecated feature is disabled, block.
  {
    NiceMock<Runtime::MockLoader> runtime;

    EXPECT_CALL(
        runtime.snapshot_,
        deprecatedFeatureEnabled("envoy.deprecated_features.allow_deprecated_extension_names", _))
        .WillRepeatedly(Return(false));

    EXPECT_EQ(ExtensionNameUtil::Status::Block,
              ExtensionNameUtil::deprecatedExtensionNameStatus(&runtime));
  }
}

// Test that deprecated names trigger an exception.
TEST(ExtensionNameUtilTest, DEPRECATED_FEATURE_TEST(TestCheckDeprecatedExtensionNameThrows)) {
  // Validate that no runtime available results in warnings.
  {
    auto test = []() {
      ExtensionNameUtil::checkDeprecatedExtensionName("XXX", "deprecated", "canonical", nullptr);
    };

    EXPECT_NO_THROW(test());

    EXPECT_LOG_CONTAINS("warn", "Using deprecated XXX extension name 'deprecated' for 'canonical'.",
                        test());
  }

  // If deprecated feature is enabled, warn.
  {
    NiceMock<Runtime::MockLoader> runtime;

    EXPECT_CALL(
        runtime.snapshot_,
        deprecatedFeatureEnabled("envoy.deprecated_features.allow_deprecated_extension_names", _))
        .WillRepeatedly(Return(true));

    auto test = [&]() {
      ExtensionNameUtil::checkDeprecatedExtensionName("XXX", "deprecated", "canonical", &runtime);
    };
    EXPECT_NO_THROW(test());

    EXPECT_LOG_CONTAINS("warn", "Using deprecated XXX extension name 'deprecated' for 'canonical'.",
                        test());
  }

  // If deprecated feature is disabled, throw.
  {
    NiceMock<Runtime::MockLoader> runtime;

    EXPECT_CALL(
        runtime.snapshot_,
        deprecatedFeatureEnabled("envoy.deprecated_features.allow_deprecated_extension_names", _))
        .WillRepeatedly(Return(false));

    EXPECT_THROW_WITH_REGEX(
        ExtensionNameUtil::checkDeprecatedExtensionName("XXX", "deprecated", "canonical", &runtime),
        EnvoyException, "Using deprecated XXX extension name 'deprecated' for 'canonical'.*");
  }
}

// Test that deprecated names are reported as allowed or not, with logging.
TEST(ExtensionNameUtilTest, DEPRECATED_FEATURE_TEST(TestAllowDeprecatedExtensionName)) {
  // Validate that no runtime available results in warnings and allows deprecated names.
  {
    auto test = []() {
      return ExtensionNameUtil::allowDeprecatedExtensionName("XXX", "deprecated", "canonical",
                                                             nullptr);
    };
    EXPECT_TRUE(test());

    EXPECT_LOG_CONTAINS("warn", "Using deprecated XXX extension name 'deprecated' for 'canonical'.",
                        test());
  }

  // If deprecated feature is enabled, log and return true.
  {
    NiceMock<Runtime::MockLoader> runtime;

    EXPECT_CALL(
        runtime.snapshot_,
        deprecatedFeatureEnabled("envoy.deprecated_features.allow_deprecated_extension_names", _))
        .WillRepeatedly(Return(true));

    auto test = [&]() {
      return ExtensionNameUtil::allowDeprecatedExtensionName("XXX", "deprecated", "canonical",
                                                             &runtime);
    };
    EXPECT_TRUE(test());

    EXPECT_LOG_CONTAINS("warn", "Using deprecated XXX extension name 'deprecated' for 'canonical'.",
                        test());
  }

  // If deprecated feature is disabled, log and return false.
  {
    NiceMock<Runtime::MockLoader> runtime;

    EXPECT_CALL(
        runtime.snapshot_,
        deprecatedFeatureEnabled("envoy.deprecated_features.allow_deprecated_extension_names", _))
        .WillRepeatedly(Return(false));

    auto test = [&]() {
      return ExtensionNameUtil::allowDeprecatedExtensionName("XXX", "deprecated", "canonical",
                                                             &runtime);
    };
    EXPECT_FALSE(test());

    EXPECT_LOG_CONTAINS("error", "#using-runtime-overrides-for-deprecated-features", test());
  }
}

} // namespace
} // namespace Utility
} // namespace Common
} // namespace Extensions
} // namespace Envoy
