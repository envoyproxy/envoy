#include "gtest/gtest.h"
#include "tools/type_whisperer/api_type_db.h"

namespace Envoy {
namespace Tools {
namespace TypeWhisperer {
namespace {

// TODO(htuch): removal API type DB.

// Validate that ApiTypeDb::getLatestTypeInformation returns nullopt when no
// type information exists.
TEST(ApiTypeDb, GetLatestTypeInformationForTypeUnknown) {
  const auto unknown_type_information = ApiTypeDb::getLatestTypeInformation("foo");
  EXPECT_EQ(absl::nullopt, unknown_type_information);
}

// Validate that ApiTypeDb::getLatestTypeInformation is idempotent when no
// upgrade occurs.
TEST(ApiTypeDb, GetLatestTypeInformationForTypeKnownNoUpgrade) {
  const auto known_type_information =
      ApiTypeDb::getLatestTypeInformation("envoy.type.v3.Int64Range");
  EXPECT_EQ("envoy.type.v3.Int64Range", known_type_information->type_name_);
  EXPECT_EQ("envoy/type/v3/range.proto", known_type_information->proto_path_);
}

} // namespace
} // namespace TypeWhisperer
} // namespace Tools
} // namespace Envoy
