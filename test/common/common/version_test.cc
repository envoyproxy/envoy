#include "common/common/version.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(VersionTest, BuildVersion) {
  auto build_version = VersionInfo::buildVersion();
  std::string version_string =
      absl::StrCat(build_version.version().major(), ".", build_version.version().minor(), ".",
                   build_version.version().patch());
  for (const auto& label : build_version.labels()) {
    absl::StrAppend(&version_string, "-", label);
  }
  EXPECT_TRUE(absl::StartsWith(version_string, BUILD_VERSION_NUMBER));
}

} // namespace Envoy
