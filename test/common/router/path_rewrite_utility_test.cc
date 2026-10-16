#include <limits>
#include <optional>

#include "source/common/common/regex.h"
#include "source/common/router/path_rewrite_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

constexpr size_t Unbounded = std::numeric_limits<size_t>::max();

TEST(GenerateNewPathTest, ReplacesThePrefix) {
  EXPECT_EQ("/new/rest", generateNewPath("/old/rest", "/old", "/new", Unbounded));
}

TEST(GenerateNewPathTest, EmptyStripReplacesNothing) {
  EXPECT_EQ("/prefix/path", generateNewPath("/path", "", "/prefix", Unbounded));
}

// A strip longer than the origin is rejected rather than underflowing the reserve size.
TEST(GenerateNewPathTest, StripLongerThanOriginIsRejected) {
  EXPECT_EQ(std::nullopt, generateNewPath("/x", "/longer-than-origin", "/new", Unbounded));
}

TEST(GenerateNewPathTest, ResultOverTheBoundIsRejected) {
  // The result `/newxx` is six bytes, so a bound of five rejects it and a bound of six accepts
  // it.
  EXPECT_EQ(std::nullopt, generateNewPath("/xx", "", "/new", 5));
  EXPECT_EQ("/new/xx", generateNewPath("/xx", "", "/new", 7));
}

TEST(RewritePathByPrefixOrRegexTest, PrefixRewriteKeepsTheQuery) {
  EXPECT_EQ("/new/rest?a=b",
            rewritePathByPrefixOrRegex("/old/rest?a=b", "/old", "/new", nullptr, "", Unbounded));
}

// The prefix match is case insensitive, matching the native prefix rewrite.
TEST(RewritePathByPrefixOrRegexTest, PrefixMatchIsCaseInsensitive) {
  EXPECT_EQ("/new/rest",
            rewritePathByPrefixOrRegex("/OLD/rest", "/old", "/new", nullptr, "", Unbounded));
}

// A matched that is not a prefix of the path is rejected rather than asserting.
TEST(RewritePathByPrefixOrRegexTest, PrefixMismatchIsRejected) {
  EXPECT_EQ(std::nullopt,
            rewritePathByPrefixOrRegex("/other", "/old", "/new", nullptr, "", Unbounded));
}

TEST(RewritePathByPrefixOrRegexTest, NoRewriteConfiguredReturnsNullopt) {
  EXPECT_EQ(std::nullopt, rewritePathByPrefixOrRegex("/path", "", "", nullptr, "", Unbounded));
}

TEST(RewritePathByPrefixOrRegexTest, RegexRewriteAppliesToThePathOnly) {
  const Regex::CompiledGoogleReMatcherNoSafetyChecks matcher("/foo/(.*)");
  EXPECT_EQ("/bar/x?q=1",
            rewritePathByPrefixOrRegex("/foo/x?q=1", "", "", &matcher, "/bar/\\1", Unbounded));
}

// A regex result over the bound is rejected before it is grown into the full path.
TEST(RewritePathByPrefixOrRegexTest, RegexResultOverTheBoundIsRejected) {
  const Regex::CompiledGoogleReMatcherNoSafetyChecks matcher("/foo");
  EXPECT_EQ(std::nullopt, rewritePathByPrefixOrRegex("/foo", "", "", &matcher, "/way-too-long", 4));
}

} // namespace
} // namespace Router
} // namespace Envoy
