#include "common/config/api_type_db.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(ApiTypeDb, GetProtoPathForTypeUnknown) {
  const auto unknown_type_path = ApiTypeDb::getProtoPathForType("foo");
  EXPECT_EQ(absl::nullopt, unknown_type_path);
}

TEST(ApiTypeDb, GetProtoPathForTypeKnown) {
  const auto known_type_path = ApiTypeDb::getProtoPathForType("envoy.type.Int64Range");
  EXPECT_EQ("envoy/type/range.proto", *known_type_path);
}

} // namespace
} // namespace Config
} // namespace Envoy
