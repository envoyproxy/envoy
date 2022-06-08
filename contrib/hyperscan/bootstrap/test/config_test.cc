#include "test/integration/base_integration_test.h"
#include "test/test_common/utility.h"

#include "contrib/hyperscan/bootstrap/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

class HyperscanIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public BaseIntegrationTest {
public:
  HyperscanIntegrationTest() : BaseIntegrationTest(GetParam(), config()) {}

  void initializeConfig(const std::string& param) {
    config_helper_.addBootstrapExtension(fmt::format(R"EOF(
name: envoy.bootstrap.hyperscan
typed_config:
  '@type': type.googleapis.com/envoy.extensions.bootstrap.hyperscan.v3alpha.Hyperscan
  {}
    )EOF",
                                                     param));
  }

  static std::string config() {
    return absl::StrCat(ConfigHelper::baseConfigNoListeners(), R"EOF(
default_regex_engine: envoy.bootstrap.hyperscan
    )EOF");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HyperscanIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that matching will be performed successfully.
TEST_P(HyperscanIntegrationTest, Hyperscan) {
  initializeConfig("");
  initialize();

  auto* engine = Envoy::Regex::engine("envoy.bootstrap.hyperscan");
  ASSERT_EQ(Envoy::Regex::EngineSingleton::getExisting(), engine);

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = "^/asdf/.+";

  Envoy::Regex::CompiledMatcherPtr regex_matcher = Envoy::Regex::Utility::parseRegex(matcher);

  EXPECT_TRUE(regex_matcher->match("/asdf/1"));
  EXPECT_FALSE(regex_matcher->match("/ASDF/1"));
};

// Verify that matching will be performed case-insensitively.
TEST_P(HyperscanIntegrationTest, HyperscanWithParam) {
  initializeConfig("caseless: true");
  initialize();

  auto* engine = Envoy::Regex::engine("envoy.bootstrap.hyperscan");
  ASSERT_EQ(Envoy::Regex::EngineSingleton::getExisting(), engine);

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

  auto* engine = Envoy::Regex::engine("envoy.bootstrap.hyperscan");
  ASSERT_EQ(Envoy::Regex::EngineSingleton::getExisting(), engine);

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = "b+";

  Envoy::Regex::CompiledMatcherPtr regex_matcher = Envoy::Regex::Utility::parseRegex(matcher);

  EXPECT_EQ(regex_matcher->replaceAll("yabba dabba doo", "d"), "yada dada doo");
};

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
