#include "test/mocks/server/factory_context.h"

#include "contrib/hyperscan/matching/input_matchers/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

class ConfigTest : public ::testing::Test {
protected:
  void setup(const std::string& yaml) {
    envoy::config::core::v3::TypedExtensionConfig config;
    config.set_name("hyperscan");
    envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan hyperscan;
    TestUtility::loadFromYaml(yaml, hyperscan);
    config.mutable_typed_config()->PackFrom(hyperscan);

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
  EXPECT_THROW_WITH_MESSAGE(setup(R"EOF(
regexes:
- regex: ^/asdf/.+
)EOF"),
                            EnvoyException, "X86_64 architecture is required for Hyperscan.");
}
#else
// Verify that matching will be performed successfully.
TEST_F(ConfigTest, Regex) {
  setup(R"EOF(
regexes:
- regex: ^/asdf/.+
)EOF");

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that matching will be performed case-insensitively.
TEST_F(ConfigTest, RegexWithCaseless) {
  setup(R"EOF(
regexes:
- regex: ^/asdf/.+
  caseless: true
)EOF");

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_TRUE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that matching a `.` will not exclude newlines.
TEST_F(ConfigTest, RegexWithDotAll) {
  setup(R"EOF(
regexes:
- regex: ^/asdf/.+
  dot_all: true
)EOF");

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_TRUE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that `^` and `$` anchors match any newlines in data.
TEST_F(ConfigTest, RegexWithMultiline) {
  setup(R"EOF(
regexes:
- regex: ^/asdf/.+
  multiline: true
)EOF");

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_TRUE(matcher_->match("\n/asdf/1"));
}

// Verify that expressions which can match against an empty string.
TEST_F(ConfigTest, RegexWithAllowEmpty) {
  setup(R"EOF(
regexes:
- regex: .*
  allow_empty: true
)EOF");

  EXPECT_TRUE(matcher_->match(""));
}

// Verify that treating the pattern as a sequence of UTF-8 characters.
TEST_F(ConfigTest, RegexWithUTF8) {
  setup(R"EOF(
regexes:
- regex: ^.$
  utf8: true
)EOF");

  EXPECT_TRUE(matcher_->match("😀"));
}

// Verify that using Unicode properties for character classes.
TEST_F(ConfigTest, RegexWithUCP) {
  setup(R"EOF(
regexes:
- regex: ^\w$
  utf8: true
  ucp: true
)EOF");

  EXPECT_TRUE(matcher_->match("Á"));
}

// Verify that using logical combination.
TEST_F(ConfigTest, RegexWithCombination) {
  setup(R"EOF(
regexes:
- regex: a
  id: 1
  quiet: true
- regex: b
  id: 2
  quiet: true
- regex: 1 | 2
  combination: true
)EOF");

  EXPECT_TRUE(matcher_->match("a"));
}
#endif

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
