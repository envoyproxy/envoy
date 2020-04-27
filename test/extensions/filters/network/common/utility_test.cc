#include "extensions/filters/network/common/utility.h"
#include "extensions/filters/network/well_known_names.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace {

// Test that canonical (or unknown) names are returned unmodified.
TEST(FilterNameUtilTest, TestIgnoreCanonicalName) {
  NiceMock<Runtime::MockLoader> runtime;

  EXPECT_EQ(NetworkFilterNames::get().RedisProxy,
            FilterNameUtil::canonicalFilterName(NetworkFilterNames::get().RedisProxy, &runtime));
  EXPECT_EQ("canonical.name", FilterNameUtil::canonicalFilterName("canonical.name", &runtime));
}

// Test that deprecated names are canonicalized.
TEST(FilterNameUtilTest, DEPRECATED_FEATURE_TEST(TestDeprecatedName)) {
  NiceMock<Runtime::MockLoader> runtime;

  EXPECT_CALL(
      runtime.snapshot_,
      deprecatedFeatureEnabled("envoy.deprecated_features.allow_deprecated_extension_names", _))
      .WillRepeatedly(Return(true));

  EXPECT_EQ(NetworkFilterNames::get().RedisProxy,
            FilterNameUtil::canonicalFilterName("envoy.redis_proxy", &runtime));
}

// Test that deprecated names trigger an exception if the deprecated name feature is disabled.
TEST(FilterNameUtilTest, TestDeprecatedNameThrows) {
  NiceMock<Runtime::MockLoader> runtime;

  EXPECT_CALL(
      runtime.snapshot_,
      deprecatedFeatureEnabled("envoy.deprecated_features.allow_deprecated_extension_names", _))
      .WillRepeatedly(Return(false));

  EXPECT_THROW_WITH_REGEX(FilterNameUtil::canonicalFilterName("envoy.redis_proxy", &runtime),
                          EnvoyException,
                          "Using deprecated network filter extension name 'envoy.redis_proxy' .*");
}

} // namespace
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
