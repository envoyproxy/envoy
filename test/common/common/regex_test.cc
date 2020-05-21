#include "envoy/common/exception.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "common/common/regex.h"
#include "common/runtime/runtime_impl.h"

#include "test/test_common/utility.h"
#include "test/test_common/logging.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "gtest/gtest.h"

using testing::NiceMock;

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
  // Runtime initialization.
  Event::MockDispatcher dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::TestUtil::TestStore store_;
  Runtime::MockRandomGenerator generator_;
  Api::ApiPtr api_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  envoy::config::bootstrap::v3::LayeredRuntime config;
  config.add_layers()->mutable_admin_layer();
  std::unique_ptr<Runtime::ScopedLoaderSingleton> loader_ = std::make_unique<Runtime::ScopedLoaderSingleton>(
      Runtime::LoaderPtr{new Runtime::LoaderImpl(dispatcher_, tls_, config, local_info_, store_,
                                                  generator_, validation_visitor_, *api_)});
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
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2();
    EXPECT_NO_THROW(Utility::parseRegex(matcher));
  }

  // Verify max program size with the deprecated field codepath plus runtime.
  // The deprecated field codepath precedes any runtime settings.
  {
    Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"regex.max_program_size_error_level", "3"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2()->mutable_max_program_size_error_level()->set_default_value(5);
    matcher.mutable_google_re2()->mutable_max_program_size_error_level()->set_runtime_key("regex.max_program_size_error_level");
    matcher.mutable_google_re2()->mutable_max_program_size()->set_value(1);
#ifndef GTEST_USES_SIMPLE_RE
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of [0-9]+ > max program size of 1\\.");
#else
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of \\d+ > max program size of 1\\.");
#endif
  }

  // Verify that an exception is thrown for the error level threshold max program size.
  {
    Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"regex.max_program_size_error_level", "1"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2()->mutable_max_program_size_error_level()->set_default_value(5);
    matcher.mutable_google_re2()->mutable_max_program_size_error_level()->set_runtime_key("regex.max_program_size_error_level");
#ifndef GTEST_USES_SIMPLE_RE
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of [0-9]+ > max program size of 1 set for the error level threshold\\.");
#else
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of \\d+ > max program size of 1 set for the error level threshold\\.");
#endif
  }

  // Verify that an exception is thrown for the error level threshold max program size without default value.
  {
    Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"regex.max_program_size_error_level", "1"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*/asdf/.*");
    matcher.mutable_google_re2()->mutable_max_program_size_error_level();
#ifndef GTEST_USES_SIMPLE_RE
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of [0-9]+ > max program size of 100 set for the error level threshold\\.");
#else
    EXPECT_THROW_WITH_REGEX(Utility::parseRegex(matcher), EnvoyException,
                            "RE2 program size of \\d+ > max program size of 100 set for the error level threshold\\.");
#endif
  }

  // Verify that a warning is logged for the warning level threshold max program size.
  {
    Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"regex.max_program_size_warning_level", "1"}});
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2()->mutable_max_program_size_warn_level()->set_default_value(5);
    matcher.mutable_google_re2()->mutable_max_program_size_warn_level()->set_runtime_key("regex.max_program_size_warning_level");
    EXPECT_NO_THROW(Utility::parseRegex(matcher));
    EXPECT_LOG_CONTAINS("warn", "> max program size of 1 set for the warning level threshold",
                      Utility::parseRegex(matcher));
  }

  // Verify that no check is performed if there is no default value or runtime key set for the warning level max program size.
  {
    envoy::type::matcher::v3::RegexMatcher matcher;
    matcher.set_regex("/asdf/.*");
    matcher.mutable_google_re2()->mutable_max_program_size_warn_level();
    EXPECT_NO_THROW(Utility::parseRegex(matcher));
  }
}

} // namespace
} // namespace Regex
} // namespace Envoy
