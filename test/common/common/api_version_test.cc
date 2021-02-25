#include "envoy/config/core/v3/api_version.pb.h"

#include "common/version/api_version.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

// Class for accessing private members of the VersionInfo class.
class ApiVersionInfoTestPeer {
public:
  static envoy::config::core::v3::ApiVersionNumber makeApiVersion(const std::string& version) {
    return ApiVersionInfo::makeApiVersion(version.c_str());
  }
  static envoy::config::core::v3::ApiVersionNumber
  computeOldestApiVersion(const std::string& latest_version_str) {
    return ApiVersionInfo::computeOldestApiVersion(
        ApiVersionInfo::makeApiVersion(latest_version_str.c_str()));
  }
};

// Verifies that api version is parsed correctly.
TEST(ApiVersionTest, MakeApiVersion) {
  const auto api_version = ApiVersionInfoTestPeer::makeApiVersion("10.20.3");
  EXPECT_EQ(10, api_version.version().major_number());
  EXPECT_EQ(20, api_version.version().minor_number());
  EXPECT_EQ(3, api_version.version().patch());
}

// Verifies that a bad api version returns zeroed version.
TEST(ApiVersionTest, MakeBadApiVersion) {
  const auto api_version = ApiVersionInfoTestPeer::makeApiVersion("3.foo.2");
  EXPECT_EQ(0, api_version.version().major_number());
  EXPECT_EQ(0, api_version.version().minor_number());
  EXPECT_EQ(0, api_version.version().patch());
}

// Verifies that the oldest API version returns a valid version.
TEST(ApiVersionTest, ValidOldestApiVersion) {
  // Pairs of latest API version and its corresponding expected oldest API version.
  const std::vector<std::pair<std::string, std::string>> expected_latest_oldest_pairs{
      {"3.2.2", "3.1.0"}, {"4.5.30", "4.4.0"}, {"1.1.5", "1.0.0"}, {"2.0.3", "2.0.0"}};
  for (const auto& latest_oldest_pair : expected_latest_oldest_pairs) {
    const auto& computed_oldest_api_version =
        ApiVersionInfoTestPeer::computeOldestApiVersion(latest_oldest_pair.first);
    const auto& expected_oldest_api_version =
        ApiVersionInfoTestPeer::makeApiVersion(latest_oldest_pair.second);
    EXPECT_EQ(expected_oldest_api_version.version().major_number(),
              computed_oldest_api_version.version().major_number());
    EXPECT_EQ(expected_oldest_api_version.version().minor_number(),
              computed_oldest_api_version.version().minor_number());
    EXPECT_EQ(expected_oldest_api_version.version().patch(),
              computed_oldest_api_version.version().patch());
  }
}

// Verify the version to string converter.
TEST(ApiVersionTest, ApiVersionToString) {
  const auto api_version1 = ApiVersionInfoTestPeer::makeApiVersion("10.20.3");
  EXPECT_EQ("10.20.3", ApiVersionInfo::apiVersionToString(api_version1));
  const auto api_version2 = ApiVersionInfoTestPeer::makeApiVersion("10.020.3");
  EXPECT_EQ("10.20.3", ApiVersionInfo::apiVersionToString(api_version2));
  const auto api_version3 = ApiVersionInfoTestPeer::makeApiVersion("10.020.bad");
  EXPECT_EQ("0.0.0", ApiVersionInfo::apiVersionToString(api_version3));
}

// Verify assumptions about oldest version vs latest version.
TEST(ApiVersionTest, OldestLatestVersionsAssumptions) {
  const auto latest_version = ApiVersionInfo::apiVersion();
  const auto oldest_version = ApiVersionInfo::oldestApiVersion();
  // Same major number, minor number difference is at most 1, and the oldest patch is 0.
  EXPECT_EQ(latest_version.version().major_number(), oldest_version.version().major_number());
  ASSERT_TRUE(latest_version.version().minor_number() >= oldest_version.version().minor_number() &&
              latest_version.version().minor_number() - oldest_version.version().minor_number() <=
                  1);
  EXPECT_EQ(0, oldest_version.version().patch());
}

} // namespace Envoy
