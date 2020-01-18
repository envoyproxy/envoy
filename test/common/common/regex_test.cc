#include "envoy/common/exception.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "common/common/regex.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Regex {
namespace {

TEST(Utility, ParseStdRegex) {
  EXPECT_THROW_WITH_REGEX(Utility::parseStdRegex("(+invalid)"), EnvoyException,
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

  // Verify max program size.
  {
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.mutable_google_re2()->mutable_max_program_size()->set_value(1);
    matcher.set_regex("/asdf/.*");
#ifndef GTEST_USES_SIMPLE_RE
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of [0-9]+ > max program size of 1\\.");
#else
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of \\d+ > max program size of 1\\.");
#endif
  }
}

} // namespace
} // namespace Regex
} // namespace Envoy
