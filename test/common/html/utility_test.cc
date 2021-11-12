#include "source/common/html/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Html {
namespace {

TEST(HttpUtility, SanitizeHtml) {
  EXPECT_EQ("simple text, no cares/worries", Utility::sanitize("simple text, no cares/worries"));
  EXPECT_EQ("a&amp;b", Utility::sanitize("a&b"));
  EXPECT_EQ("&lt;script&gt;", Utility::sanitize("<script>"));
}

} // namespace
} // namespace Html
} // namespace Envoy
