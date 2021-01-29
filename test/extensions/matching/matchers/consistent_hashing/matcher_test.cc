#include "extensions/matching/matchers/consistent_hashing/matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Matchers {
namespace ConsistentHashing {

TEST(MatcherTest, EmptyValue) {
  Matcher matcher(10, 100);

  ASSERT_FALSE(matcher.match(absl::nullopt));
}

TEST(MatcherTest, BasicUsage) {
  {
    Matcher matcher(58, 100);

    // The string 'hello' hashes to 2794345569481354659
    // With mod 100 this results in 59, which is greater
    // than the threshold.
    ASSERT_TRUE(matcher.match("hello"));
  }

  Matcher matcher(59, 100);

  // Changing the threshold to 59 means that we no longer match.
  ASSERT_FALSE(matcher.match("hello"));
}
} // namespace ConsistentHashing
} // namespace Matchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy