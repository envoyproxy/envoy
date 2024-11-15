#include "envoy/common/exception.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "source/common/common/regex.h"

#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Regex {
namespace {

TEST(Utility, ParseRegex) {
  Regex::GoogleReEngine engine;
  {
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.mutable_google_re2();
    matcher.set_regex("(+invalid)");
    EXPECT_THROW_WITH_MESSAGE(Utility::parseRegex(matcher, engine), EnvoyException,
                              "no argument for repetition operator: +");
  }

  // Regression test for https://github.com/envoyproxy/envoy/issues/7728
  {
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.mutable_google_re2();
    matcher.set_regex("/asdf/.*");
    const auto compiled_matcher = Utility::parseRegex(matcher, engine);
    const std::string long_string = "/asdf/" + std::string(50 * 1024, 'a');
    EXPECT_TRUE(compiled_matcher->match(long_string));
  }

  // Regression test for https://github.com/envoyproxy/envoy/issues/15826
  {
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.mutable_google_re2();
    matcher.set_regex("/status/200(/.*)?$");
    const auto compiled_matcher = Utility::parseRegex(matcher, engine);
    EXPECT_TRUE(compiled_matcher->match("/status/200"));
    EXPECT_TRUE(compiled_matcher->match("/status/200/"));
    EXPECT_TRUE(compiled_matcher->match("/status/200/foo"));
    EXPECT_FALSE(compiled_matcher->match("/status/200foo"));
  }

  // Positive case to ensure no max program size is enforced.
  {
    TestScopedRuntime scoped_runtime;
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2();
    EXPECT_NO_THROW(Utility::parseRegex(matcher, engine));
  }

  // Positive case to ensure matcher can be created by config without google_re2 field.
  {
    TestScopedRuntime scoped_runtime;
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    EXPECT_NO_THROW(Utility::parseRegex(matcher, engine));
  }

  // Verify max program size with the deprecated field codepath plus runtime.
  // The deprecated field codepath precedes any runtime settings.
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({{"re2.max_program_size.error_level", "3"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2()->mutable_max_program_size()->set_value(1);
#ifndef GTEST_USES_SIMPLE_RE
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher, engine), EnvoyException,
                            "RE2 program size of [0-9]+ > max program size of 1\\.");
#else
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher, engine), EnvoyException,
                            "RE2 program size of \\d+ > max program size of 1\\.");
#endif
  }

  // Verify that an exception is thrown for the error level max program size.
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({{"re2.max_program_size.error_level", "1"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2();
#ifndef GTEST_USES_SIMPLE_RE
    EXPECT_THROW_WITH_REGEX(
        Utility::parseRegex(matcher, engine), EnvoyException,
        "RE2 program size of [0-9]+ > max program size of 1 set for the error level threshold\\.");
#else
    EXPECT_THROW_WITH_REGEX(
        Utility::parseRegex(matcher, engine), EnvoyException,
        "RE2 program size of \\d+ > max program size of 1 set for the error level threshold\\.");
#endif
  }

  // Verify that the error level max program size defaults to 100 if not set by runtime.
  {
    TestScopedRuntime scoped_runtime;
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex(
        "/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*");
    matcher.mutable_google_re2();
#ifndef GTEST_USES_SIMPLE_RE
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher, engine), EnvoyException,
                            "RE2 program size of [0-9]+ > max program size of 100 set for the "
                            "error level threshold\\.");
#else
    EXPECT_THROW_WITH_REGEX(
        Utility::parseRegex(matcher, engine), EnvoyException,
        "RE2 program size of \\d+ > max program size of 100 set for the error level threshold\\.");
#endif
  }

  // Verify that a warning is logged for the warn level max program size.
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({{"re2.max_program_size.warn_level", "1"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2();
    EXPECT_NO_THROW(Utility::parseRegex(matcher, engine));
    EXPECT_LOG_CONTAINS("warn", "> max program size of 1 set for the warn level threshold",
                        Utility::parseRegex(matcher, engine));
  }

  // Verify that no check is performed if the warn level max program size is not set by runtime.
  {
    TestScopedRuntime scoped_runtime;
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2();
    EXPECT_NO_THROW(Utility::parseRegex(matcher, engine));
    EXPECT_LOG_NOT_CONTAINS("warn", "> max program size", Utility::parseRegex(matcher, engine));
  }
}

} // namespace
} // namespace Regex
} // namespace Envoy
