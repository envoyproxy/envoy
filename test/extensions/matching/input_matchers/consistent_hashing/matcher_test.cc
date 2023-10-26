#include "source/extensions/matching/input_matchers/consistent_hashing/matcher.h"

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
    Matcher matcher1(10, 100, 0);
    Matcher matcher2(10, 100, 0);

    EXPECT_FALSE(matcher1.match(absl::monostate()));
    EXPECT_FALSE(matcher2.match(absl::monostate()));
  }
  {
    Matcher matcher1(58, 100, 0);
    Matcher matcher2(58, 100, 0);

    // The string 'hello' hashes to 2794345569481354659
    // With mod 100 this results in 59, which is greater
    // than the threshold.
    EXPECT_TRUE(matcher1.match("hello"));
    EXPECT_TRUE(matcher2.match("hello"));
  }
  {
    Matcher matcher1(59, 100, 0);
    Matcher matcher2(59, 100, 0);

    // The string 'hello' hashes to 2794345569481354659
    // With mod 100 this results in 59, which is equal
    // to the threshold.
    EXPECT_TRUE(matcher1.match("hello"));
    EXPECT_TRUE(matcher2.match("hello"));
  }
  {
    Matcher matcher1(60, 100, 0);
    Matcher matcher2(60, 100, 0);

    // The string 'hello' hashes to 2794345569481354659
    // With mod 100 this results in 59, which is less
    // than the threshold.
    EXPECT_FALSE(matcher1.match("hello"));
    EXPECT_FALSE(matcher2.match("hello"));
  }
  {
    Matcher matcher1(0, 1, 0);
    Matcher matcher2(0, 1, 0);

    // The string 'hello' hashes to 2794345569481354659
    // With mod 1 this results in 0, which is equal to
    // the threshold.
    EXPECT_TRUE(matcher1.match("hello"));
    EXPECT_TRUE(matcher2.match("hello"));
  }
  {
    Matcher matcher1(80, 100, 0);
    Matcher matcher2(80, 100, 13221);

    // The string 'hello' hashes to 2794345569481354659 with seed 0
    // and to 10451234660802341186 with seed 13221.
    // This means that with seed 0 the string is below the threshold,
    // while for seed 13221 the value is above the threshold.
    EXPECT_FALSE(matcher1.match("hello"));
    EXPECT_TRUE(matcher2.match("hello"));
  }
}
} // namespace ConsistentHashing
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
