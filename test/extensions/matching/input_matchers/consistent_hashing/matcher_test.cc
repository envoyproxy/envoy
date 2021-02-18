#include "extensions/matching/input_matchers/consistent_hashing/matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace ConsistentHashing {

// Validates that two independent matchers agree on the
// match result for various inputs.
TEST(MatcherTest, BasicUsage) {
  {
    Matcher matcher1(10, 100);
    Matcher matcher2(10, 100);

    EXPECT_FALSE(matcher1.match(absl::nullopt));
    EXPECT_FALSE(matcher2.match(absl::nullopt));
  }
  {
    Matcher matcher1(58, 100);
    Matcher matcher2(58, 100);

    // The string 'hello' hashes to 2794345569481354659
    // With mod 100 this results in 59, which is greater
    // than the threshold.
    EXPECT_TRUE(matcher1.match("hello"));
    EXPECT_TRUE(matcher2.match("hello"));
  }

  Matcher matcher1(59, 100);
  Matcher matcher2(59, 100);

  // Changing the threshold to 59 means that we no longer match.
  EXPECT_FALSE(matcher1.match("hello"));
  EXPECT_FALSE(matcher2.match("hello"));
}
} // namespace ConsistentHashing
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
