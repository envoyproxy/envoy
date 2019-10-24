#include "extensions/tracers/xray/util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

TEST(XRayWildcardTest, MatchingEmpty) {
  ASSERT_TRUE(wildcardMatch("", ""));
  ASSERT_FALSE(wildcardMatch("", "42"));
  ASSERT_TRUE(wildcardMatch("*", ""));
  ASSERT_FALSE(wildcardMatch("?", ""));
}

TEST(XRayWildcardTest, MatchIdentityCaseInsensitive) {
  ASSERT_TRUE(wildcardMatch("foo", "foo"));
  ASSERT_TRUE(wildcardMatch("foo", "FOO"));
  ASSERT_TRUE(wildcardMatch("foo", "Foo"));
  ASSERT_TRUE(wildcardMatch("6543210", "6543210"));
}

TEST(XRayWildcardTest, MatchIdentityExtra) {
  ASSERT_FALSE(wildcardMatch("foo", "foob"));
  ASSERT_FALSE(wildcardMatch("foo", "xfoo"));
  ASSERT_FALSE(wildcardMatch("foo", "bar"));
}

TEST(XRayWildcardTest, SingleWildcard) {
  ASSERT_FALSE(wildcardMatch("f?o", "boo"));
  ASSERT_TRUE(wildcardMatch("fo?", "foo"));
}

TEST(XRayWildcardTest, MultipleWildcards) {
  ASSERT_FALSE(wildcardMatch("f??", "boo"));
  ASSERT_TRUE(wildcardMatch("he??o", "Hello"));
  ASSERT_TRUE(wildcardMatch("?o?", "foo"));
}

TEST(XRayWildcardTest, GlobMatch) {
  ASSERT_TRUE(wildcardMatch("f?o*ba*", "foobazbar"));
  ASSERT_TRUE(wildcardMatch("*oo", "foo"));
  ASSERT_TRUE(wildcardMatch("*o?", "foo"));
  ASSERT_TRUE(wildcardMatch("mis*spell", "mistily spell"));
  ASSERT_TRUE(wildcardMatch("mis*spell", "misspell"));
}

TEST(XRayWildcardTest, GlobMismatch) {
  ASSERT_FALSE(wildcardMatch("foo*", "fo0"));
  ASSERT_FALSE(wildcardMatch("fo*obar", "foobaz"));
  ASSERT_FALSE(wildcardMatch("mis*spellx", "mispellx"));
  ASSERT_FALSE(wildcardMatch("f?*", "boo"));
}

TEST(XRayWildcardTest, OnlyGlob) {
  ASSERT_TRUE(wildcardMatch("*", "foo"));
  ASSERT_TRUE(wildcardMatch("*", "anything"));
  ASSERT_TRUE(wildcardMatch("*", "12354"));
  ASSERT_TRUE(wildcardMatch("*", "UPPERCASE"));
  ASSERT_TRUE(wildcardMatch("*", "miXEDcaSe"));
  ASSERT_TRUE(wildcardMatch("*******", "Envoy"));
}

TEST(XRayWildcardTest, LengthAtLeastTwo) {
  EXPECT_FALSE(wildcardMatch("??*", "a"));
  EXPECT_TRUE(wildcardMatch("??*", "aa"));
  EXPECT_TRUE(wildcardMatch("??*", "aaa"));
}
} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
