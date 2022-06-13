#include "envoy/extensions/regex_engines/v3/google_re2.pb.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "test/integration/base_integration_test.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class RegexEngineIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public BaseIntegrationTest {
public:
  RegexEngineIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfigNoListeners()) {}

  void initializeConfig(uint32_t max_program_size, uint32_t warn_program_size) {
    config_helper_.addConfigModifier(
        [max_program_size, warn_program_size](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          envoy::extensions::regex_engines::v3::GoogleRE2 google_re2;
          google_re2.mutable_max_program_size()->set_value(max_program_size);
          google_re2.mutable_warn_program_size()->set_value(warn_program_size);
          bootstrap.mutable_default_regex_engine()->set_name("envoy.regex_engines.google_re2");
          bootstrap.mutable_default_regex_engine()->mutable_typed_config()->PackFrom(google_re2);
        });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RegexEngineIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(RegexEngineIntegrationTest, GoogleRE2) {
  initialize();

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = ".*";

  EXPECT_NO_THROW(Regex::Utility::parseRegex(matcher));
};

TEST_P(RegexEngineIntegrationTest, GoogleRE2WithMaxProgramSize) {
  initializeConfig(1, UINT32_MAX);
  initialize();

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = ".*";

  EXPECT_THROW_WITH_REGEX(Regex::Utility::parseRegex(matcher), EnvoyException, "max program size");
}

TEST_P(RegexEngineIntegrationTest, GoogleRE2WithWarnProgramSize) {
  initializeConfig(100, 1);
  initialize();

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = ".*";

  EXPECT_LOG_CONTAINS("warning", "warn program size", Regex::Utility::parseRegex(matcher));
}

} // namespace
} // namespace Envoy
