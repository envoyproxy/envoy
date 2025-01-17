#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/tenant_id.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Test calculation using an empty string
TEST(TenantIdTest, TestEmpty) { EXPECT_EQ(absl::StrCat(calculateTenantId("")), "0"); }

// Test calculation using strings with unexpected characters
TEST(TenantIdTest, TestUnexpected) {
  EXPECT_EQ(absl::StrCat(calculateTenantId("abc 1234")), "182ccac");
  EXPECT_EQ(absl::StrCat(calculateTenantId("      ")), "b173ef2e");
  EXPECT_EQ(absl::StrCat(calculateTenantId("€someth")), "17bd71ec");
}

// Test calculation using some expected strings
TEST(TenantIdTest, TestValues) {
  EXPECT_EQ(absl::StrCat(calculateTenantId("jmw13303")), "4d10bede");
  EXPECT_EQ(absl::StrCat(calculateTenantId("abc12345")), "5b3f9fed");
  EXPECT_EQ(absl::StrCat(calculateTenantId("?pfel")), "7712d29d");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
