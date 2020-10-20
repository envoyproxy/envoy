#include "extensions/filters/common/rbac/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace {

TEST(ResponseDetail, ResponseDetail) {
  EXPECT_EQ(RBAC::responseDetail("abdfxy"), "rbac_access_denied_matched_policy[abdfxy]");
  EXPECT_EQ(RBAC::responseDetail("ab df  xy"), "rbac_access_denied_matched_policy[ab_df__xy]");
  EXPECT_EQ(RBAC::responseDetail("a \t\f\v\n\ry"), "rbac_access_denied_matched_policy[a______y]");
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
