#include "source/common/version/api_version.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

// Verify assumptions about oldest version vs latest version.
TEST(ApiVersionTest, OldestLatestVersionsAssumptions) {
  constexpr auto latest_version = ApiVersionInfo::apiVersion();
  constexpr auto oldest_version = ApiVersionInfo::oldestApiVersion();
  // Same major number, minor number difference is at most 1, and the oldest patch is 0.
  EXPECT_EQ(latest_version.major, oldest_version.major);
  EXPECT_TRUE(latest_version.minor >= oldest_version.minor &&
              latest_version.minor - oldest_version.minor <= 1);
  EXPECT_EQ(0, oldest_version.patch);
}

} // namespace Envoy
