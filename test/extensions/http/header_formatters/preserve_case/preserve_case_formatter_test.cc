#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

TEST(PreserveCaseFormatterTest, All) {
  PreserveCaseHeaderFormatter formatter(false,
                                        envoy::extensions::http::header_formatters::preserve_case::
                                            v3::PreserveCaseFormatterConfig::DEFAULT);
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

TEST(PreserveCaseFormatterTest, ReasonPhraseEnabled) {
  PreserveCaseHeaderFormatter formatter(true,
                                        envoy::extensions::http::header_formatters::preserve_case::
                                            v3::PreserveCaseFormatterConfig::DEFAULT);

  formatter.setReasonPhrase(absl::string_view("Slow Down"));

  EXPECT_EQ("Slow Down", formatter.getReasonPhrase());
}

TEST(PreserveCaseFormatterTest, ReasonPhraseDisabled) {
  PreserveCaseHeaderFormatter formatter(false,
                                        envoy::extensions::http::header_formatters::preserve_case::
                                            v3::PreserveCaseFormatterConfig::DEFAULT);

  formatter.setReasonPhrase(absl::string_view("Slow Down"));

  EXPECT_TRUE(formatter.getReasonPhrase().empty());
}

TEST(PreserveCaseFormatterTest, ProperCaseFormatterOnEnvoyHeadersEnabled) {
  PreserveCaseHeaderFormatter formatter(false,
                                        envoy::extensions::http::header_formatters::preserve_case::
                                            v3::PreserveCaseFormatterConfig::PROPER_CASE);
  formatter.processKey("Foo");
  formatter.processKey("Bar");
  formatter.processKey("BAR");

  EXPECT_EQ("Foo", formatter.format("foo"));
  EXPECT_EQ("Foo", formatter.format("Foo"));
  EXPECT_EQ("Bar", formatter.format("bar"));
  EXPECT_EQ("Bar", formatter.format("Bar"));
  EXPECT_EQ("Bar", formatter.format("BAR"));
  EXPECT_EQ("Baz", formatter.format("baz"));
  EXPECT_EQ("Hello-World", formatter.format("hello-world"));
  EXPECT_EQ("Hello#WORLD", formatter.format("hello#wORLD"));

  EXPECT_EQ(true, formatter.formatterOnEnvoyHeaders().has_value());
}

TEST(PreserveCaseFormatterTest, DefaultFormatterOnEnvoyHeadersEnabled) {
  PreserveCaseHeaderFormatter formatter(false,
                                        envoy::extensions::http::header_formatters::preserve_case::
                                            v3::PreserveCaseFormatterConfig::DEFAULT);
  formatter.processKey("Foo");
  formatter.processKey("Bar");
  formatter.processKey("BAR");

  EXPECT_EQ("Foo", formatter.format("foo"));
  EXPECT_EQ("Foo", formatter.format("Foo"));
  EXPECT_EQ("Bar", formatter.format("bar"));
  EXPECT_EQ("Bar", formatter.format("Bar"));
  EXPECT_EQ("Bar", formatter.format("BAR"));
  EXPECT_EQ("baz", formatter.format("baz"));
  EXPECT_EQ("hello-world", formatter.format("hello-world"));
  EXPECT_EQ("hello#wORLD", formatter.format("hello#wORLD"));

  EXPECT_EQ(false, formatter.formatterOnEnvoyHeaders().has_value());
}

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
