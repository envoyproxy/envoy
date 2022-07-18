#include "source/extensions/pattern_template/pattern_template_matching.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {

TEST(PatternTemplate, RouteMatcher) {
  matching::UrlTemplatePredicate matcher("/foo/{lang}/{country}", "rewrite");

  EXPECT_TRUE(matcher.match("/foo/english/us"));
  EXPECT_TRUE(matcher.match("/foo/spanish/spain"));
  EXPECT_TRUE(matcher.match("/foo/french/france"));

  // with params
  EXPECT_TRUE(matcher.match("/foo/english/us#fragment"));
  EXPECT_TRUE(matcher.match("/foo/spanish/spain#fragment?param=val"));
  EXPECT_TRUE(matcher.match("/foo/french/france?param=regex"));

  EXPECT_FALSE(matcher.match("/foo/english/us/goat"));
  EXPECT_FALSE(matcher.match("/foo/goat"));
  EXPECT_FALSE(matcher.match("/foo"));
  EXPECT_FALSE(matcher.match(""));

  // with params
  EXPECT_FALSE(matcher.match("/foo/english/us/goat#fragment?param=val"));
  EXPECT_FALSE(matcher.match("/foo/goat?param=regex"));
  EXPECT_FALSE(matcher.match("/foo?param=regex"));
}

} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
