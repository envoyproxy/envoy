#include <utility>
#include <vector>

#include "source/common/common/regex.h"

#include "test/test_common/utility.h"

#include "contrib/hyperscan/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

static const std::string yaml_string = R"EOF(
engine:
  name: google-re2
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.hyperscan.v3alpha.Hyperscan{}
regex: {}
)EOF";

static const std::string option_string = R"EOF(

    {}: {}
)EOF";

class ConfigTest : public ::testing::Test {
public:
  ConfigTest() = default;

  void setup(const absl::string_view regex) {
    envoy::type::matcher::v3::RegexMatcher matcher;
    TestUtility::loadFromYaml(fmt::format(yaml_string, "", regex), matcher);
    matcher_ =
        Envoy::Regex::Utility::parseRegex(matcher, ProtobufMessage::getStrictValidationVisitor());
  }

  void setupWithOptions(
      const absl::string_view regex,
      std::vector<std::pair<const absl::string_view, const absl::string_view>> options) {
    std::string option_strs;
    for (auto& option : options) {
      option_strs += fmt::format(option_string, option.first, option.second);
    }

    envoy::type::matcher::v3::RegexMatcher matcher;
    TestUtility::loadFromYaml(fmt::format(yaml_string, option_strs, regex), matcher);
    matcher_ =
        Envoy::Regex::Utility::parseRegex(matcher, ProtobufMessage::getStrictValidationVisitor());
  }

  Envoy::Regex::CompiledMatcherPtr matcher_;
};

// Verify that default matching will be performed successfully.
TEST_F(ConfigTest, Regex) {
  setup("^/asdf/.+");

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that matching will be performed case-insensitively.
TEST_F(ConfigTest, RegexWithCaseless) {
  setupWithOptions("^/asdf/.+", {{"caseless", "true"}});

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_TRUE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that matching a `.` will not exclude newlines.
TEST_F(ConfigTest, RegexWithDotAll) {
  setupWithOptions("^/asdf/.+", {{"dot_all", "true"}});

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_TRUE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that `^` and `$` anchors match any newlines in data.
TEST_F(ConfigTest, RegexWithMultiline) {
  setupWithOptions("^/asdf/.+", {{"multiline", "true"}});

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_TRUE(matcher_->match("\n/asdf/1"));
}

// Verify that expressions which can match against an empty string is not allowed.
TEST_F(ConfigTest, RegexWithoutAllowEmpty) {
  EXPECT_THROW_WITH_MESSAGE(
      setup(".*"), EnvoyException,
      "Pattern matches empty buffer; use HS_FLAG_ALLOWEMPTY to enable support.");
}

// Verify that expressions which can match against an empty string is allowed.
TEST_F(ConfigTest, RegexWithAllowEmpty) {
  setupWithOptions(".*", {{"allow_empty", "true"}});

  EXPECT_TRUE(matcher_->match(""));
}

// Verify that not treating the pattern as a sequence of UTF-8 characters.
TEST_F(ConfigTest, RegexWithoutUTF8) {
  setup("^.$");

  EXPECT_FALSE(matcher_->match("😀"));
}

// Verify that treating the pattern as a sequence of UTF-8 characters.
TEST_F(ConfigTest, RegexWithUTF8) {
  setupWithOptions("^.$", {{"utf8", "true"}});

  EXPECT_TRUE(matcher_->match("😀"));
}

// Verify that not using Unicode properties for character classes.
TEST_F(ConfigTest, RegexWithoutUCP) {
  setupWithOptions("^\\w$", {{"utf8", "true"}});

  EXPECT_FALSE(matcher_->match("Á"));
}

// Verify that using Unicode properties for character classes.
TEST_F(ConfigTest, RegexWithUCP) {
  setupWithOptions("^\\w$", {{"utf8", "true"}, {"ucp", "true"}});

  EXPECT_TRUE(matcher_->match("Á"));
}

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
