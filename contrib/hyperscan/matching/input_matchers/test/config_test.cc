#include "test/mocks/server/factory_context.h"

#include "contrib/hyperscan/matching/input_matchers/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

constexpr absl::string_view yaml_string = R"EOF(
name: hyperscan
typed_config:
  "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.hyperscan.v3alpha.Hyperscan
  regexes:
  - regex: {}
    {}
)EOF";

class ConfigTest : public ::testing::Test {
protected:
  void setup(const std::string& expression, const std::string& param = "") {
    envoy::config::core::v3::TypedExtensionConfig config;
    TestUtility::loadFromYaml(fmt::format(std::string(yaml_string), expression, param), config);

    Config factory;
    auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
        config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);
    matcher_ = factory.createInputMatcherFactoryCb(*message, context_)();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Envoy::Matcher::InputMatcherPtr matcher_;
};

#ifdef HYPERSCAN_DISABLED
// Verify that incompatible architecture will cause a throw.
TEST_F(ConfigTest, IncompatibleArchitecture) {
  EXPECT_THROW_WITH_MESSAGE(setup("^/asdf/.+"), EnvoyException,
                            "X86_64 architecture is required for Hyperscan.");
}
#else
// Verify that matching will be performed successfully.
TEST_F(ConfigTest, Regex) {
  setup("^/asdf/.+");

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
}

// Verify that matching will be performed successfully with parameters set.
TEST_F(ConfigTest, RegexWithParam) {
  setup("^/asdf/.+", "caseless: true");

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_TRUE(matcher_->match("/ASDF/1"));
}
#endif

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
