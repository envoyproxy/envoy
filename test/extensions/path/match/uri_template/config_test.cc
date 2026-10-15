#include "source/common/config/utility.h"
#include "source/extensions/path/match/uri_template/config.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Match {

using ::Envoy::StatusHelpers::HasStatusMessage;

TEST(ConfigTest, TestEmptyConfig) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.uri_template.uri_template_matcher
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatcherFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.match.uri_template.uri_template_matcher");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  ProtobufTypes::MessagePtr empty_config = factory->createEmptyConfigProto();

  EXPECT_NE(nullptr, empty_config);
}

TEST(ConfigTest, InvalidConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.uri_template.uri_template_matcher
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
        path_template: "/bar/{lang}/{country"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatcherFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.match.uri_template.uri_template_matcher");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathMatcherSharedPtr> config_or_error =
      factory->createPathMatcher(*message);

  EXPECT_THAT(
      config_or_error,
      HasStatusMessage(
          "path_match_policy.path_template /bar/{lang}/{country is invalid: Unmatched variable "
          "bracket in mixed pattern: \"{country\""));
}

// Followup on issue https://github.com/envoyproxy/envoy/issues/34507 -
// providing more details on errors.
TEST(ConfigTest, InvalidConfigSetupMoreInfo) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.uri_template.uri_template_matcher
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
        path_template: "/api/MyFunction[{id}]"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatcherFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.match.uri_template.uri_template_matcher");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathMatcherSharedPtr> config_or_error =
      factory->createPathMatcher(*message);

  EXPECT_THAT(config_or_error,
              HasStatusMessage("path_match_policy.path_template /api/MyFunction[{id}] is invalid: "
                               "Invalid prefix literal: "
                               "\"MyFunction[\""));
}

TEST(ConfigTest, TestConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.uri_template.uri_template_matcher
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatcherFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.match.uri_template.uri_template_matcher");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathMatcherSharedPtr> config_or_error =
      factory->createPathMatcher(*message);

  EXPECT_OK(config_or_error);
}

} // namespace Match
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
