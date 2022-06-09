#include "envoy/type/matcher/v3/regex.pb.h"

#include "source/common/common/regex_engine.h"

#include "test/integration/base_integration_test.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class RegexEngineIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public BaseIntegrationTest {
public:
  RegexEngineIntegrationTest() : BaseIntegrationTest(GetParam(), config()) {}

  void initializeConfig(const std::string& param) {
    config_helper_.addBootstrapExtension(fmt::format(R"EOF(
name: envoy.extensions.regex_engine.google_re2
typed_config:
  '@type': type.googleapis.com/envoy.extensions.regex_engine.v3.GoogleRE2
  {}
    )EOF",
                                                     param));
  }

  static std::string config() {
    return absl::StrCat(ConfigHelper::baseConfigNoListeners(), R"EOF(
default_regex_engine: envoy.extensions.regex_engine.google_re2
    )EOF");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RegexEngineIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(RegexEngineIntegrationTest, GoogleRE2) {
  initializeConfig("");
  initialize();

  auto* engine = Regex::engine("envoy.extensions.regex_engine.google_re2");
  ASSERT_EQ(Regex::EngineSingleton::getExisting(), engine);

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = ".*";

  EXPECT_NO_THROW(Regex::Utility::parseRegex(matcher));
};

TEST_P(RegexEngineIntegrationTest, GoogleRE2WithMaxProgramSize) {
  initializeConfig("max_program_size: 1");
  initialize();

  auto* engine = Regex::engine("envoy.extensions.regex_engine.google_re2");
  ASSERT_EQ(Regex::EngineSingleton::getExisting(), engine);

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = ".*";

  EXPECT_THROW_WITH_REGEX(Regex::Utility::parseRegex(matcher), EnvoyException, "max program size");
}

TEST_P(RegexEngineIntegrationTest, GoogleRE2WithWarnProgramSize) {
  initializeConfig("warn_program_size: 1");
  initialize();

  auto* engine = Regex::engine("envoy.extensions.regex_engine.google_re2");
  ASSERT_EQ(Regex::EngineSingleton::getExisting(), engine);

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = ".*";

  EXPECT_LOG_CONTAINS("warning", "warn program size", Regex::Utility::parseRegex(matcher));
}

} // namespace
} // namespace Envoy
