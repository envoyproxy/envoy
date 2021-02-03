#include "common/protobuf/type_util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {
TEST(TypeUtilTest, TypeUrlHelperFunction) {
  EXPECT_EQ("envoy.config.filter.http.ip_tagging.v2.IPTagging",
            TypeUtil::typeUrlToDescriptorFullName(
                "type.googleapis.com/envoy.config.filter.http.ip_tagging.v2.IPTagging"));
  EXPECT_EQ(
      "type.googleapis.com/envoy.config.filter.http.ip_tagging.v2.IPTagging",
      TypeUtil::descriptorFullNameToTypeUrl("envoy.config.filter.http.ip_tagging.v2.IPTagging"));
}
} // namespace
} // namespace Config
} // namespace Envoy