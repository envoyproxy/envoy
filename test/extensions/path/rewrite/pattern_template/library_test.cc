#include "source/common/config/utility.h"
#include "source/extensions/path/match/pattern_template/pattern_template_match.h"
#include "source/extensions/path/rewrite/pattern_template/config.h"
#include "source/extensions/path/rewrite/pattern_template/pattern_template_rewrite.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

Router::PathMatchPredicateSharedPtr createMatchPredicateFromYaml(std::string yaml_string) {
  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatchPredicateFactory>(config);

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  absl::StatusOr<Router::PathMatchPredicateSharedPtr> config_or_error =
      factory->createPathMatchPredicate(*message);

  return config_or_error.value();
}

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

TEST(RewriteTest, RewriteInvalidRegex) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewritePredicateSharedPtr predicate = createRewritePredicateFromYaml(yaml_string);
  absl::StatusOr<std::string> rewrite_or_error =
      predicate->rewriteUrl("/bar/en/usa", "/bar/invalid}/{lang}");
  EXPECT_FALSE(rewrite_or_error.ok());
  EXPECT_EQ(rewrite_or_error.status().message(), "Unable to parse url pattern regex");
}

TEST(RewriteTest, MatchPatternValidation) {
  const std::string rewrite_yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/foo/{lang}/{country}"
)EOF";

  const std::string match_yaml_string = R"EOF(
      name: envoy.path.match.pattern_template.pattern_template_match_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.pattern_template.v3.PatternTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewritePredicateSharedPtr rewrite_predicate =
      createRewritePredicateFromYaml(rewrite_yaml_string);
  Router::PathMatchPredicateSharedPtr match_predicate =
      createMatchPredicateFromYaml(match_yaml_string);

  EXPECT_TRUE(rewrite_predicate->isCompatibleMatchPolicy(match_predicate, true).ok());
}

TEST(RewriteTest, MatchPatternInactive) {
  const std::string rewrite_yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/foo/{lang}/{country}"
)EOF";

  const std::string match_yaml_string = R"EOF(
      name: envoy.path.match.pattern_template.pattern_template_match_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.pattern_template.v3.PatternTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewritePredicateSharedPtr rewrite_predicate =
      createRewritePredicateFromYaml(rewrite_yaml_string);
  Router::PathMatchPredicateSharedPtr match_predicate =
      createMatchPredicateFromYaml(match_yaml_string);

  absl::Status error = rewrite_predicate->isCompatibleMatchPolicy(match_predicate, false);
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.message(),
            "unable to use envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate "
            "extension without envoy.path.match.pattern_template.pattern_template_match_predicate "
            "extension");
}

TEST(RewriteTest, MatchPatternMismatchedVars) {
  const std::string rewrite_yaml_string = R"EOF(
      name: envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.pattern_template.v3.PatternTemplateRewriteConfig
        path_template_rewrite: "/foo/{lang}/{missing}"
)EOF";

  const std::string match_yaml_string = R"EOF(
      name: envoy.path.match.pattern_template.pattern_template_match_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.pattern_template.v3.PatternTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewritePredicateSharedPtr rewrite_predicate =
      createRewritePredicateFromYaml(rewrite_yaml_string);
  Router::PathMatchPredicateSharedPtr match_predicate =
      createMatchPredicateFromYaml(match_yaml_string);

  absl::Status error = rewrite_predicate->isCompatibleMatchPolicy(match_predicate, true);
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.message(), "mismatch between variables in path_match_policy "
                             "/bar/{lang}/{country} and path_rewrite_policy /foo/{lang}/{missing}");
}

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
