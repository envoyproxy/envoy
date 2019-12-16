#include "gtest/gtest.h"
#include "tools/type_whisperer/api_type_db.h"

namespace Envoy {
namespace Tools {
namespace TypeWhisperer {
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
} // namespace TypeWhisperer
} // namespace Tools
} // namespace Envoy
