#include "source/common/config/utility.h"
#include "source/extensions/path/rewrite/pattern_template/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

TEST(ConfigTest, TestEmptyConfig) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewritePredicateFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(),
            "envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  ProtobufTypes::MessagePtr empty_config = factory->createEmptyConfigProto();

  EXPECT_NE(nullptr, empty_config);
}

TEST(ConfigTest, InvalidConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewritePredicateFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(),
            "envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathRewritePredicateSharedPtr> config_or_error =
      factory->createPathRewritePredicate(*message);

  EXPECT_FALSE(config_or_error.ok());
  EXPECT_EQ(config_or_error.status().message(),
            "path_rewrite_policy.path_template_rewrite /bar/{lang}/{country is invalid");
}

TEST(ConfigTest, TestConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewritePredicateFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(),
            "envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathRewritePredicateSharedPtr> config_or_error =
      factory->createPathRewritePredicate(*message);

  EXPECT_TRUE(config_or_error.ok());
}

TEST(ConfigTest, TestInvalidConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewritePredicateFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(),
            "envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathRewritePredicateSharedPtr> config_or_error =
      factory->createPathRewritePredicate(*message);

  EXPECT_FALSE(config_or_error.ok());
  EXPECT_EQ(config_or_error.message(), "path_rewrite_policy.path_template_rewrite /bar/{lang}/country} is invalid"");
}

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
