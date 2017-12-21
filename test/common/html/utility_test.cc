#include "common/html/utility.h"

#include "testing/base/public/gunit.h"

namespace Envoy {
namespace Html {

TEST(HttpUtility, sanitizeHtml) {
  EXPECT_EQ("simple text, no cares/worries", Utility::sanitize("simple text, no cares/worries"));
  EXPECT_EQ("a&amp;b", Utility::sanitize("a&b"));
  EXPECT_EQ("&lt;script&gt;", Utility::sanitize("<script>"));
}

} // namespace Http
} // namespace Envoy
