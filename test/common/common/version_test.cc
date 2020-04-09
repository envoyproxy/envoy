#include "common/common/version.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

// Class for accessing private members of the VersionInfo class.
class VersionInfoTestPeer {
public:
  static const std::string& buildType() { return VersionInfo::buildType(); }
  static const std::string& sslVersion() { return VersionInfo::sslVersion(); }
  static envoy::config::core::v3::BuildVersion makeBuildVersion(const char* version) {
    return VersionInfo::makeBuildVersion(version);
  }
};

TEST(VersionTest, BuildVersion) {
  auto build_version = VersionInfo::buildVersion();
  std::string version_string =
      absl::StrCat(build_version.version().major_number(), ".",
                   build_version.version().minor_number(), ".", build_version.version().patch());

  const auto& fields = build_version.metadata().fields();
  if (fields.find(BuildVersionMetadataKeys::get().BuildLabel) != fields.end()) {
    absl::StrAppend(&version_string, "-",
                    fields.at(BuildVersionMetadataKeys::get().BuildLabel).string_value());
  }
  EXPECT_EQ(BUILD_VERSION_NUMBER, version_string);
  EXPECT_EQ(VersionInfo::revision(),
            fields.at(BuildVersionMetadataKeys::get().RevisionSHA).string_value());
  EXPECT_EQ(VersionInfo::revisionStatus(),
            fields.at(BuildVersionMetadataKeys::get().RevisionStatus).string_value());
  EXPECT_EQ(VersionInfoTestPeer::buildType(),
            fields.at(BuildVersionMetadataKeys::get().BuildType).string_value());
  EXPECT_EQ(VersionInfoTestPeer::sslVersion(),
            fields.at(BuildVersionMetadataKeys::get().SslVersion).string_value());
}

TEST(VersionTest, MakeBuildVersionWithLabel) {
  auto build_version = VersionInfoTestPeer::makeBuildVersion("1.2.3-foo-bar");
  EXPECT_EQ(1, build_version.version().major_number());
  EXPECT_EQ(2, build_version.version().minor_number());
  EXPECT_EQ(3, build_version.version().patch());
  const auto& fields = build_version.metadata().fields();
  EXPECT_GE(fields.size(), 1);
  EXPECT_EQ("foo-bar", fields.at(BuildVersionMetadataKeys::get().BuildLabel).string_value());
}

TEST(VersionTest, MakeBuildVersionWithoutLabel) {
  auto build_version = VersionInfoTestPeer::makeBuildVersion("1.2.3");
  EXPECT_EQ(1, build_version.version().major_number());
  EXPECT_EQ(2, build_version.version().minor_number());
  EXPECT_EQ(3, build_version.version().patch());
  const auto& fields = build_version.metadata().fields();
  EXPECT_EQ(fields.find(BuildVersionMetadataKeys::get().BuildLabel), fields.end());
  // Other metadata should still be present
  EXPECT_GE(fields.size(), 1);
}

TEST(VersionTest, MakeBadBuildVersion) {
  auto build_version = VersionInfoTestPeer::makeBuildVersion("1.foo.3-bar");
  EXPECT_EQ(0, build_version.version().major_number());
  EXPECT_EQ(0, build_version.version().minor_number());
  EXPECT_EQ(0, build_version.version().patch());
  const auto& fields = build_version.metadata().fields();
  EXPECT_EQ(fields.find(BuildVersionMetadataKeys::get().BuildLabel), fields.end());
  // Other metadata should still be present
  EXPECT_GE(fields.size(), 1);
}

} // namespace Envoy
