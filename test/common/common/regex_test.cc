#include "envoy/common/exception.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "common/common/regex.h"

#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Regex {
namespace {

TEST(Utility, ParseStdRegex) {
  EXPECT_THROW_WITH_REGEX(Utility::parseStdRegex("(+invalid)"), EnvoyException,
                          "Invalid regex '\\(\\+invalid\\)': .+");

  EXPECT_THROW_WITH_REGEX(Utility::parseStdRegexAsCompiledMatcher("(+invalid)"), EnvoyException,
                          "Invalid regex '\\(\\+invalid\\)': .+");

  {
    std::regex regex = Utility::parseStdRegex("x*");
    EXPECT_NE(0, regex.flags() & std::regex::optimize);
  }

  {
    std::regex regex = Utility::parseStdRegex("x*", std::regex::icase);
    EXPECT_NE(0, regex.flags() & std::regex::icase);
    EXPECT_EQ(0, regex.flags() & std::regex::optimize);
  }

  {
    // Regression test to cover high-complexity regular expressions that throw on std::regex_match.
    // Note that not all std::regex_match implementations will throw when matching against the
    // expression below, but at least clang 9.0.0 under linux does.
    auto matcher = Utility::parseStdRegexAsCompiledMatcher(
        "|||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||");
    EXPECT_FALSE(matcher->match("0"));
  }
}

TEST(Utility, ParseRegex) {
  {
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.mutable_google_re2();
    matcher.set_regex("(+invalid)");
    EXPECT_THROW_WITH_MESSAGE(Utility::parseRegex(matcher), EnvoyException,
                              "no argument for repetition operator: +");
  }

  // Regression test for https://github.com/envoyproxy/envoy/issues/7728
  {
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.mutable_google_re2();
    matcher.set_regex("/asdf/.*");
    const auto compiled_matcher = Utility::parseRegex(matcher);
    const std::string long_string = "/asdf/" + std::string(50 * 1024, 'a');
    EXPECT_TRUE(compiled_matcher->match(long_string));
  }

  // Positive case to ensure no max program size is enforced.
  {
    TestScopedRuntime scoped_runtime;
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2();
    EXPECT_NO_THROW(Utility::parseRegex(matcher));
  }

  // Verify max program size with the deprecated field codepath plus runtime.
  // The deprecated field codepath precedes any runtime settings.
  {
    TestScopedRuntime scoped_runtime;
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"re2.max_program_size.error_level", "3"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2()->mutable_max_program_size()->set_value(1);
#ifndef GTEST_USES_SIMPLE_RE
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of [0-9]+ > max program size of 1\\.");
#else
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of \\d+ > max program size of 1\\.");
#endif
  }

  // Verify that an exception is thrown for the error level max program size.
  {
    TestScopedRuntime scoped_runtime;
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"re2.max_program_size.error_level", "1"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2();
#ifndef GTEST_USES_SIMPLE_RE
    EXPECT_THROW_WITH_REGEX(
        Utility::parseRegex(matcher), EnvoyException,
        "RE2 program size of [0-9]+ > max program size of 1 set for the error level threshold\\.");
#else
    EXPECT_THROW_WITH_REGEX(
        Utility::parseRegex(matcher), EnvoyException,
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
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of [0-9]+ > max program size of 100 set for the "
                            "error level threshold\\.");
#else
    EXPECT_THROW_WITH_REGEX(
        Utility::parseRegex(matcher), EnvoyException,
        "RE2 program size of \\d+ > max program size of 100 set for the error level threshold\\.");
#endif
  }

  // Verify that a warning is logged for the warn level max program size.
  {
    TestScopedRuntime scoped_runtime;
    Envoy::Stats::Counter& warn_count =
        Runtime::LoaderSingleton::getExisting()->getRootScope().counterFromString(
            "re2.exceeded_warn_level");
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"re2.max_program_size.warn_level", "1"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2();
    EXPECT_NO_THROW(Utility::parseRegex(matcher));
    EXPECT_EQ(1, warn_count.value());
    EXPECT_LOG_CONTAINS("warn", "> max program size of 1 set for the warn level threshold",
                        Utility::parseRegex(matcher));
    EXPECT_EQ(2, warn_count.value());
  }

  // Verify that no check is performed if the warn level max program size is not set by runtime.
  {
    TestScopedRuntime scoped_runtime;
    Envoy::Stats::Counter& warn_count =
        Runtime::LoaderSingleton::getExisting()->getRootScope().counterFromString(
            "re2.exceeded_warn_level");
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2();
    EXPECT_NO_THROW(Utility::parseRegex(matcher));
    EXPECT_LOG_NOT_CONTAINS("warn", "> max program size", Utility::parseRegex(matcher));
    EXPECT_EQ(0, warn_count.value());
  }
}

} // namespace
} // namespace Regex
} // namespace Envoy
