#include "test/integration/base_integration_test.h"
#include "test/test_common/utility.h"

#include "contrib/hyperscan/regex_engines/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

class HyperscanIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public BaseIntegrationTest {
public:
  HyperscanIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfigNoListeners()) {}

  void initializeConfig(bool caseless) {
    config_helper_.addConfigModifier(
        [caseless](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          envoy::extensions::regex_engines::hyperscan::v3alpha::Hyperscan hyperscan;
          if (caseless) {
            hyperscan.set_caseless(true);
          }
          bootstrap.mutable_default_regex_engine()->set_name("envoy.regex_engines.hyperscan");
          bootstrap.mutable_default_regex_engine()->mutable_typed_config()->PackFrom(hyperscan);
        });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HyperscanIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that matching will be performed successfully.
TEST_P(HyperscanIntegrationTest, Hyperscan) {
  initializeConfig(false);
  initialize();

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = "^/asdf/.+";

  Envoy::Regex::CompiledMatcherPtr regex_matcher = Envoy::Regex::Utility::parseRegex(matcher);

  EXPECT_TRUE(regex_matcher->match("/asdf/1"));
  EXPECT_FALSE(regex_matcher->match("/ASDF/1"));
};

// Verify that matching will be performed case-insensitively.
TEST_P(HyperscanIntegrationTest, HyperscanWithParam) {
  initializeConfig(true);
  initialize();

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = "^/asdf/.+";

  Envoy::Regex::CompiledMatcherPtr regex_matcher = Envoy::Regex::Utility::parseRegex(matcher);

  EXPECT_TRUE(regex_matcher->match("/asdf/1"));
  EXPECT_TRUE(regex_matcher->match("/ASDF/1"));
};

// Verify that matching will be performed successfully.
TEST_P(HyperscanIntegrationTest, ReplaceAll) {
  initializeConfig("");
  initialize();

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = "b+";

  Envoy::Regex::CompiledMatcherPtr regex_matcher = Envoy::Regex::Utility::parseRegex(matcher);

  EXPECT_EQ(regex_matcher->replaceAll("yabba dabba doo", "d"), "yada dada doo");
};

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
