#include "source/common/version/version.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(ContribVersionTest, VersionContainsSuffix) {
  EXPECT_THAT(VersionInfo::version(), testing::HasSubstr("-contrib/"));
}

TEST(ContribVersionTest, BuildVersionContainsSuffix) {
  auto build_version = VersionInfo::buildVersion();
  const auto& fields = build_version.metadata().fields();
  ASSERT_TRUE(fields.contains(BuildVersionMetadataKeys::get().BuildLabel));
  EXPECT_THAT(fields.at(BuildVersionMetadataKeys::get().BuildLabel).string_value(),
              testing::EndsWith("contrib"));
}

} // namespace Envoy
