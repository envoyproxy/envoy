#include "envoy/common/exception.h"

#include "common/common/url_template_matcher.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matchers {
namespace {

TEST(UrlTemplateMatcherTest, OnlyLiteral) {
  UrlTemplateMatcher m("/foo/bar");
  EXPECT_TRUE(m.match("/foo/bar"));
  EXPECT_FALSE(m.match("/bar"));
}

TEST(UrlTemplateMatcherTest, WildCard) {
  // * match 1 segment.
  UrlTemplateMatcher m("/foo/*");
  EXPECT_TRUE(m.match("/foo/bar"));
  EXPECT_FALSE(m.match("/foo"));
  EXPECT_FALSE(m.match("/foo/zoo/bar"));
  EXPECT_FALSE(m.match("/bar"));
}

TEST(UrlTemplateMatcherTest, WildCardWithVariable) {
  // {x} is the same as *: match 1 segment.
  UrlTemplateMatcher m("/foo/{x}");
  EXPECT_TRUE(m.match("/foo/bar"));
  EXPECT_FALSE(m.match("/foo"));
  EXPECT_FALSE(m.match("/foo/zoo/bar"));
  EXPECT_FALSE(m.match("/bar"));
}

TEST(UrlTemplateMatcherTest, DoubleWildCard) {
  // ** match zero or more segments
  UrlTemplateMatcher m("/foo/**");
  EXPECT_TRUE(m.match("/foo"));
  EXPECT_TRUE(m.match("/foo/bar"));
  EXPECT_TRUE(m.match("/foo/zoo/bar"));
  EXPECT_FALSE(m.match("/bar"));
}

TEST(UrlTemplateMatcherTest, DoubleWildCardWithVariable) {
  // ** match zero or more segments
  UrlTemplateMatcher m("/foo/**/tail");
  EXPECT_TRUE(m.match("/foo/bar/tail"));
  EXPECT_TRUE(m.match("/foo/zoo/bar/tail"));

  EXPECT_FALSE(m.match("/foo/other"));
  EXPECT_FALSE(m.match("/foo/bar/other"));
  EXPECT_FALSE(m.match("/foo/zoo/bar/other"));

  EXPECT_FALSE(m.match("/bar/tail"));
}

TEST(UrlTemplateMatcherTest, InvalidUrlTemplate) {
  EXPECT_THROW_WITH_MESSAGE(UrlTemplateMatcher("/{}"), EnvoyException, "invalid url_template: /{}");
}

} // namespace
} // namespace Matchers
} // namespace Envoy
