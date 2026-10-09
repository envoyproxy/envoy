#include "source/common/version/version.h"

#include "test/test_common/struct_matchers.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/crypto.h"

using testing::Contains;
using testing::HasSubstr;
using testing::IsSupersetOf;

namespace Envoy {

// Class for accessing private members of the VersionInfo class.
class VersionInfoTestPeer {
public:
  static const std::string& buildType() { return VersionInfo::buildType(); }
  static const std::string& sslVersion() { return VersionInfo::sslVersion(); }
  static bool sslFipsCompliant() { return VersionInfo::sslFipsCompliant(); }
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
  EXPECT_THAT(fields, IsSupersetOf(StructMatchers(
                          IsStructString(BuildVersionMetadataKeys::get().RevisionSHA,
                                         VersionInfo::revision()),
                          IsStructString(BuildVersionMetadataKeys::get().RevisionStatus,
                                         VersionInfo::revisionStatus()),
                          IsStructString(BuildVersionMetadataKeys::get().BuildType,
                                         VersionInfoTestPeer::buildType()))));
  if (FIPS_mode() == 1) {
    EXPECT_TRUE(VersionInfoTestPeer::sslFipsCompliant());
  } else {
    EXPECT_FALSE(VersionInfoTestPeer::sslFipsCompliant());
  }
  EXPECT_THAT(fields, Contains(IsStructString(BuildVersionMetadataKeys::get().SslVersion,
                                              VersionInfoTestPeer::sslVersion())));
}

TEST(VersionTest, MakeBuildVersionWithLabel) {
  auto build_version = VersionInfoTestPeer::makeBuildVersion("1.2.3-foo-bar");
  EXPECT_EQ(1, build_version.version().major_number());
  EXPECT_EQ(2, build_version.version().minor_number());
  EXPECT_EQ(3, build_version.version().patch());
  const auto& fields = build_version.metadata().fields();
  EXPECT_GE(fields.size(), 1);
  if (FIPS_mode() == 1) {
    EXPECT_TRUE(VersionInfoTestPeer::sslFipsCompliant());
  } else {
    EXPECT_FALSE(VersionInfoTestPeer::sslFipsCompliant());
  }
  EXPECT_THAT(fields,
              Contains(IsStructString(BuildVersionMetadataKeys::get().BuildLabel, "foo-bar")));
}

TEST(VersionTest, MakeBuildVersionWithoutLabel) {
  auto build_version = VersionInfoTestPeer::makeBuildVersion("1.2.3");
  EXPECT_EQ(1, build_version.version().major_number());
  EXPECT_EQ(2, build_version.version().minor_number());
  EXPECT_EQ(3, build_version.version().patch());
  const auto& fields = build_version.metadata().fields();
  EXPECT_FALSE(fields.contains(BuildVersionMetadataKeys::get().BuildLabel));
  // Other metadata should still be present
  EXPECT_GE(fields.size(), 1);
}

TEST(VersionTest, MakeBadBuildVersion) {
  auto build_version = VersionInfoTestPeer::makeBuildVersion("1.foo.3-bar");
  EXPECT_EQ(0, build_version.version().major_number());
  EXPECT_EQ(0, build_version.version().minor_number());
  EXPECT_EQ(0, build_version.version().patch());
  const auto& fields = build_version.metadata().fields();
  EXPECT_FALSE(fields.contains(BuildVersionMetadataKeys::get().BuildLabel));
  // Other metadata should still be present
  EXPECT_GE(fields.size(), 1);
}

TEST(VersionTest, VersionSuffixDefault) {
  const std::string& version = VersionInfo::version();
  EXPECT_THAT(version, HasSubstr(std::string("/") + BUILD_VERSION_NUMBER + "/"));
}

} // namespace Envoy
