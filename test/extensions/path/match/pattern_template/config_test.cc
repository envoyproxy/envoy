#include "source/common/config/utility.h"
#include "source/extensions/path/match/pattern_template/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Match {


TEST(ConfigTest, TestEmptyConfig) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.pattern_template.pattern_template_match_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.pattern_template.v3.PatternTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatchPredicateFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.match.pattern_template.pattern_template_match_predicate");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  ProtobufTypes::MessagePtr empty_config = factory->createEmptyConfigProto();

  EXPECT_NE(nullptr, empty_config);
}

TEST(ConfigTest, InvalidConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.pattern_template.pattern_template_match_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.pattern_template.v3.PatternTemplateMatchConfig
        path_template: "/bar/{lang}/{country"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatchPredicateFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.match.pattern_template.pattern_template_match_predicate");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathMatchPredicateSharedPtr> config_or_error =
      factory->createPathMatchPredicate(*message);

  EXPECT_FALSE(config_or_error.ok());
  EXPECT_EQ(config_or_error.status().message(), "path_match_policy.path_template /bar/{lang}/{country is invalid");
}

TEST(ConfigTest, TestConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.pattern_template.pattern_template_match_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.pattern_template.v3.PatternTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatchPredicateFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.match.pattern_template.pattern_template_match_predicate");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathMatchPredicateSharedPtr> config_or_error =
      factory->createPathMatchPredicate(*message);

  EXPECT_TRUE(config_or_error.ok());
}

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
