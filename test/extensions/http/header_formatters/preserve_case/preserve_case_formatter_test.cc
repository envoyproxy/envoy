#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

TEST(PreserveCaseFormatterTest, All) {
  PreserveCaseHeaderFormatter formatter(false);
  formatter.processKey("Foo");
  formatter.processKey("Bar");
  formatter.processKey("BAR");

  EXPECT_EQ("Foo", formatter.format("foo"));
  EXPECT_EQ("Foo", formatter.format("Foo"));
  EXPECT_EQ("Bar", formatter.format("bar"));
  EXPECT_EQ("Bar", formatter.format("Bar"));
  EXPECT_EQ("Bar", formatter.format("BAR"));
  EXPECT_EQ("baz", formatter.format("baz"));
}

TEST(PreserveCaseFormatterTest, ReasonPhrase) {
  PreserveCaseHeaderFormatter formatter(true);

  formatter.setReasonPhrase(absl::string_view("Slow Down"));

  EXPECT_EQ("Slow Down", formatter.getReasonPhrase());
}

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
