#ifndef HYPERSCAN_DISABLED
#include "test/integration/base_integration_test.h"
#include "test/test_common/utility.h"

#include "contrib/hyperscan/regex_engines/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

class HyperscanRegexEngineIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  HyperscanRegexEngineIntegrationTest() : BaseIntegrationTest(GetParam(), config()) {}

  static std::string config() {
    return absl::StrCat(ConfigHelper::baseConfigNoListeners(), R"EOF(
default_regex_engine:
  name: envoy.regex_engines.hyperscan
  typed_config:
    '@type': type.googleapis.com/envoy.extensions.regex_engines.hyperscan.v3alpha.Hyperscan
    )EOF");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HyperscanRegexEngineIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that matcher can be populate successfully.
TEST_P(HyperscanRegexEngineIntegrationTest, Hyperscan) {
  initialize();

  envoy::type::matcher::v3::RegexMatcher matcher;
  *matcher.mutable_regex() = "^/asdf/.+";

  EXPECT_NO_THROW(Envoy::Regex::Utility::parseRegex(matcher, test_server_->server().regexEngine()));
};

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
#endif
