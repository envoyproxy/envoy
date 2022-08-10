#include "source/extensions/path/rewrite/pattern_template/pattern_template_rewrite.h"

#include "source/common/config/utility.h"
#include "source/extensions/path/rewrite/pattern_template/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

Router::PathRewritePredicateSharedPtr createRewritePredicateFromYaml(std::string yaml_string) {
  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewritePredicateFactory>(config);

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  absl::StatusOr<Router::PathRewritePredicateSharedPtr> config_or_error =
      factory->createPathRewritePredicate(*message);

  return config_or_error.value();
}

TEST(RewriteTest, BasicSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewritePredicateSharedPtr predicate = createRewritePredicateFromYaml(yaml_string);
  EXPECT_EQ(predicate->pattern(), "/bar/{lang}/{country}");
  EXPECT_EQ(predicate->name(),
            "envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate");
}

TEST(RewriteTest, BasicUsage) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewritePredicateSharedPtr predicate = createRewritePredicateFromYaml(yaml_string);
  EXPECT_EQ(predicate->rewriteUrl("/bar/en/usa", "/bar/{country}/{lang}").value(), "/bar/usa/en");
  EXPECT_EQ(predicate->name(),
            "envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate");
}

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
