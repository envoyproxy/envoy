#include "envoy/config/core/v3/api_version.pb.h"

#include "common/version/api_version.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

// Class for accessing private members of the ApiVersionInfo class.
class ApiVersionInfoTestPeer {
public:
  static ApiVersion computeOldestApiVersion(const ApiVersion& latest_version) {
    return ApiVersionInfo::computeOldestApiVersion(latest_version);
  }
};

// Verifies that the oldest API version returns a valid version.
TEST(ApiVersionTest, ValidOldestApiVersion) {
  // Pairs of latest API version and its corresponding expected oldest API version.
  const std::vector<std::pair<ApiVersion, ApiVersion>> expected_latest_oldest_pairs{
      {ApiVersion{3, 2, 2}, ApiVersion{3, 1, 0}},
      {ApiVersion{4, 5, 30}, ApiVersion{4, 4, 0}},
      {ApiVersion{1, 1, 5}, ApiVersion{1, 0, 0}},
      {ApiVersion{2, 0, 3}, ApiVersion{2, 0, 0}}};
  for (const auto& latest_oldest_pair : expected_latest_oldest_pairs) {
    const auto& computed_oldest_api_version =
        ApiVersionInfoTestPeer::computeOldestApiVersion(latest_oldest_pair.first);
    EXPECT_EQ(latest_oldest_pair.second.major, computed_oldest_api_version.major);
    EXPECT_EQ(latest_oldest_pair.second.minor, computed_oldest_api_version.minor);
    EXPECT_EQ(latest_oldest_pair.second.patch, computed_oldest_api_version.patch);
  }
}

// Verify assumptions about oldest version vs latest version.
TEST(ApiVersionTest, OldestLatestVersionsAssumptions) {
  const auto& latest_version = ApiVersionInfo::apiVersion();
  const auto& oldest_version = ApiVersionInfo::oldestApiVersion();
  // Same major number, minor number difference is at most 1, and the oldest patch is 0.
  EXPECT_EQ(latest_version.major, oldest_version.major);
  EXPECT_TRUE(latest_version.minor >= oldest_version.minor &&
              latest_version.minor - oldest_version.minor <= 1);
  EXPECT_EQ(0, oldest_version.patch);
}

} // namespace Envoy
