#include "extensions/tracers/xray/util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

TEST(XRayUtilTest, WildcardMatchingInvalidArgs) {
  const std::string pattern = "";
  const std::string text = "whatever";
  ASSERT_FALSE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, WildcardMatchExactPositive) {
  const std::string pattern = "foo";
  const std::string text = "foo";
  ASSERT_TRUE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, WildcardMatchExactNegative) {
  const std::string pattern = "foo";
  const std::string text = "bar";
  ASSERT_FALSE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, singleWildcardPositive) {
  const std::string pattern = "fo?";
  const std::string text = "foo";
  ASSERT_TRUE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, singleWildcardNegative) {
  const std::string pattern = "f?o";
  const std::string text = "boo";
  ASSERT_FALSE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, multipleWildcardPositive) {
  const std::string pattern = "?o?";
  const std::string text = "foo";
  ASSERT_TRUE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, multipleWildcardNegative) {
  const std::string pattern = "f??";
  const std::string text = "boo";
  ASSERT_FALSE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globPositive) {
  const std::string pattern = "*oo";
  const std::string text = "foo";
  ASSERT_TRUE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globPositiveZeroOrMore) {
  const std::string pattern = "f?o*ba*";
  const std::string text = "foobazbar";
  ASSERT_TRUE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globNegativeZeroOrMore) {
  const std::string pattern = "foo*";
  const std::string text = "fo0";
  ASSERT_FALSE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globNegative) {
  const std::string pattern = "fo*";
  const std::string text = "boo";
  ASSERT_FALSE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globAndSinglePositive) {
  const std::string pattern = "*o?";
  const std::string text = "foo";
  ASSERT_TRUE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globAndSingleNegative) {
  const std::string pattern = "f?*";
  const std::string text = "boo";
  ASSERT_FALSE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, pureWildcard) {
  const std::string pattern = "*";
  const std::string text = "foo";
  ASSERT_TRUE(WildcardMatch(pattern, text));
}

TEST(XRayUtilTest, exactMatch) {
  const std::string pattern = "6543210";
  const std::string text = "6543210";
  ASSERT_TRUE(WildcardMatch(pattern, text));
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
