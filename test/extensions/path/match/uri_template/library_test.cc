#include "source/common/config/utility.h"
#include "source/extensions/path/match/uri_template/config.h"
#include "source/extensions/path/match/uri_template/uri_template_match.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Match {

Router::PathMatcherSharedPtr createMatcherFromYaml(std::string yaml_string) {
  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatcherFactory>(config);

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  absl::StatusOr<Router::PathMatcherSharedPtr> config_or_error =
      factory->createPathMatcher(*message);

  return config_or_error.value();
}

TEST(MatchTest, BasicUsage) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.uri_template.uri_template_matcher
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  Router::PathMatcherSharedPtr matcher = createMatcherFromYaml(yaml_string);
  EXPECT_EQ(matcher->uriTemplate(), "/bar/{lang}/{country}");
  EXPECT_EQ(matcher->name(), "envoy.path.match.uri_template.uri_template_matcher");

  EXPECT_TRUE(matcher->match("/bar/en/us"));
}

} // namespace Match
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
