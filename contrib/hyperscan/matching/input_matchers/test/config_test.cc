#include <utility>
#include <vector>

#include "test/mocks/server/factory_context.h"

#include "contrib/hyperscan/matching/input_matchers/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

static const std::string yaml_string = R"EOF(
name: hyperscan
typed_config:
  "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.hyperscan.v3alpha.Hyperscan
  regexes:{}
)EOF";

static const std::string regex_string = R"EOF(

  - regex: {}{}
)EOF";

static const std::string option_string = R"EOF(

    {}: {}
)EOF";

class ConfigTest : public ::testing::Test {
public:
  ConfigTest() = default;

  void
  setup(const std::vector<
        std::pair<const absl::string_view,
                  const std::vector<std::pair<const absl::string_view, const absl::string_view>>>>
            setup_configs) {
    std::string regex_strs;
    for (auto& setup_config : setup_configs) {
      std::string option_strs;
      for (auto& option : setup_config.second) {
        option_strs += fmt::format(option_string, option.first, option.second);
      }
      regex_strs += fmt::format(regex_string, setup_config.first, option_strs);
    }
    envoy::config::core::v3::TypedExtensionConfig config;
    TestUtility::loadFromYaml(fmt::format(yaml_string, regex_strs), config);

    Config factory;
    auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
        config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);
    matcher_ = factory.createInputMatcherFactoryCb(*message, context_)();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Envoy::Matcher::InputMatcherPtr matcher_;
};

// Verify that matching will be performed successfully.
TEST_F(ConfigTest, Regex) {
  setup({{"^/asdf/.+", {}}});

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that matching will be performed case-insensitively.
TEST_F(ConfigTest, RegexWithCaseless) {
  setup({{"^/asdf/.+", {{"caseless", "true"}}}});

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_TRUE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that matching a `.` will not exclude newlines.
TEST_F(ConfigTest, RegexWithDotAll) {
  setup({{"^/asdf/.+", {{"dot_all", "true"}}}});

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_TRUE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that `^` and `$` anchors match any newlines in data.
TEST_F(ConfigTest, RegexWithMultiline) {
  setup({{"^/asdf/.+", {{"multiline", "true"}}}});

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_TRUE(matcher_->match("\n/asdf/1"));
}

// Verify that expressions which can match against an empty string.
TEST_F(ConfigTest, RegexWithAllowEmpty) {
  setup({{".*", {{"allow_empty", "true"}}}});

  EXPECT_TRUE(matcher_->match(""));
}

// Verify that treating the pattern as a sequence of UTF-8 characters.
TEST_F(ConfigTest, RegexWithUTF8) {
  setup({{"^.$", {{"utf8", "true"}}}});

  EXPECT_TRUE(matcher_->match("😀"));
}

// Verify that using Unicode properties for character classes.
TEST_F(ConfigTest, RegexWithUCP) {
  setup({{"^\\w$", {{"utf8", "true"}, {"ucp", "true"}}}});

  EXPECT_TRUE(matcher_->match("Á"));
}

// Verify that using logical combination.
TEST_F(ConfigTest, RegexWithCombination) {
  setup({{"a", {{"id", "1"}, {"quiet", "true"}}},
         {"b", {{"id", "2"}, {"quiet", "true"}}},
         {"1 | 2", {{"combination", "true"}}}});

  EXPECT_TRUE(matcher_->match("a"));
}

// Verify that invalid expression will cause a throw.
TEST_F(ConfigTest, InvalidRegex) {
  EXPECT_THROW_WITH_MESSAGE(
      setup({{"(", {}}}), EnvoyException,
      "unable to compile pattern '(': Missing close parenthesis for group started at index 0.");
}

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
