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
};

TEST(VersionTest, BuildVersion) {
  auto build_version = VersionInfo::buildVersion();
  std::string version_string =
      absl::StrCat(build_version.version().major(), ".", build_version.version().minor(), ".",
                   build_version.version().patch());

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

} // namespace Envoy
