#include "source/common/config/utility.h"
#include "source/extensions/path/match/pattern_template/config.h"
#include "source/extensions/path/match/pattern_template/pattern_template_match.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Match {

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

TEST(MatchTest, BasicUsage) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.pattern_template.pattern_template_match_predicate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.pattern_template.v3.PatternTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  Router::PathMatchPredicateSharedPtr predicate = createMatchPredicateFromYaml(yaml_string);
  EXPECT_EQ(predicate->pattern(), "/bar/{lang}/{country}");
  EXPECT_EQ(predicate->name(),
            "envoy.path.match.pattern_template.pattern_template_match_predicate");

  EXPECT_TRUE(predicate->match("/bar/en/us"));
}

} // namespace Match
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
