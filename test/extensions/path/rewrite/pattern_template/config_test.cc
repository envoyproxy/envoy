#include "source/common/config/utility.h"
#include "source/extensions/path/rewrite/uri_template/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Rewrite {

TEST(ConfigTest, TestEmptyConfig) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewriterFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  ProtobufTypes::MessagePtr empty_config = factory->createEmptyConfigProto();

  EXPECT_NE(nullptr, empty_config);
}

TEST(ConfigTest, InvalidConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewritere
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewriterFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathRewriterSharedPtr> config_or_error =
      factory->createPathRewriter(*message);

  EXPECT_FALSE(config_or_error.ok());
  EXPECT_EQ(config_or_error.status().message(),
            "path_rewrite_policy.path_template_rewrite /bar/{lang}/{country is invalid");
}

TEST(ConfigTest, TestConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewriterFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathRewriterSharedPtr> config_or_error =
      factory->createPathRewriter(*message);

  EXPECT_TRUE(config_or_error.ok());
}

TEST(ConfigTest, TestInvalidConfigSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/country}"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewriterFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  EXPECT_NE(nullptr, message);

  absl::StatusOr<Router::PathRewriterSharedPtr> config_or_error =
      factory->createPathRewriter(*message);

  EXPECT_FALSE(config_or_error.ok());
  EXPECT_EQ(config_or_error.status().message(),
            "path_rewrite_policy.path_template_rewrite /bar/{lang}/country} is invalid");
}

} // namespace Rewrite
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
