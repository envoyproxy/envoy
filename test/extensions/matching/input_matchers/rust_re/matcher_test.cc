#include "source/extensions/matching/input_matchers/rust_re/matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace RustRe {

TEST(MatcherTest, BasicUsage) {
  Matcher matcher("t.*t");

  EXPECT_TRUE(matcher.match("test"));
  EXPECT_FALSE(matcher.match("foo"));
}
} // namespace RustRe
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
