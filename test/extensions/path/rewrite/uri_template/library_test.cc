#include "source/common/config/utility.h"
#include "source/extensions/path/match/uri_template/uri_template_match.h"
#include "source/extensions/path/rewrite/uri_template/config.h"
#include "source/extensions/path/rewrite/uri_template/uri_template_rewrite.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Rewrite {

using ::Envoy::StatusHelpers::HasStatusMessage;

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

Router::PathRewriterSharedPtr createRewriterFromYaml(std::string yaml_string) {
  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathRewriterFactory>(config);

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  absl::StatusOr<Router::PathRewriterSharedPtr> config_or_error =
      factory->createPathRewriter(*message);

  return config_or_error.value();
}

TEST(RewriteTest, BasicSetup) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  EXPECT_EQ(rewriter->uriTemplate(), "/bar/{lang}/{country}");
  EXPECT_EQ(rewriter->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");
}

TEST(RewriteTest, BasicUsage) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  EXPECT_EQ(rewriter->rewritePath("/bar/en/usa", "/bar/{country}/{lang}").value(), "/bar/usa/en");
  EXPECT_EQ(rewriter->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");
}

TEST(RewriteTest, SingleAsteriskAtEndOfPath) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{country}/{final}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  EXPECT_EQ(rewriter->rewritePath("/bar/usa/final*1", "/bar/{final}/{country}").value(),
            "/bar/final*1/usa");
  EXPECT_EQ(rewriter->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");
}

TEST(RewriteTest, SingleAsterisk) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{country}/final"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  EXPECT_EQ(rewriter->rewritePath("/bar/usa*/final", "/bar/{country}/final").value(),
            "/bar/usa*/final");
  EXPECT_EQ(rewriter->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");
}

TEST(RewriteTest, DoubleAsteriskAtEndOfPath) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{country}/{final}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  EXPECT_EQ(rewriter->rewritePath("/bar/usa/final**1", "/bar/{final}/{country}").value(),
            "/bar/final**1/usa");
  EXPECT_EQ(rewriter->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");
}

TEST(RewriteTest, DoubleAsterisk) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{country}/final"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  EXPECT_EQ(rewriter->rewritePath("/bar/usa**/final", "/bar/{country}/final").value(),
            "/bar/usa**/final");
  EXPECT_EQ(rewriter->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");
}

TEST(RewriteTest, DoubleEqualAtEndOfPath) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{country}/{final}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  EXPECT_EQ(rewriter->rewritePath("/bar/usa/final==1", "/bar/{final}/{country}").value(),
            "/bar/final==1/usa");
  EXPECT_EQ(rewriter->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");
}

TEST(RewriteTest, DoubleEqual) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{country}/final"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  EXPECT_EQ(rewriter->rewritePath("/bar/usa==/final", "/bar/{country}/final").value(),
            "/bar/usa==/final");
  EXPECT_EQ(rewriter->name(), "envoy.path.rewrite.uri_template.uri_template_rewriter");
}

TEST(RewriteTest, PatternNotMatched) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}/{test}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  absl::StatusOr<std::string> rewrite_or_error =
      rewriter->rewritePath("/bar/en/usa", "/bar/{country}/{lang}/{test}");
  EXPECT_THAT(rewrite_or_error, HasStatusMessage("Pattern not match"));
}

TEST(RewriteTest, RewriteInvalidRegex) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  absl::StatusOr<std::string> rewrite_or_error =
      rewriter->rewritePath("/bar/en/usa", "/bar/invalid}/{lang}");
  EXPECT_THAT(rewrite_or_error, HasStatusMessage("Unable to parse matched_path"));
}

TEST(RewriteTest, MatchPatternValidation) {
  const std::string rewrite_yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/foo/{lang}/{country}"
)EOF";

  const std::string match_yaml_string = R"EOF(
      name: envoy.path.match.uri_template.uri_template_matcher
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(rewrite_yaml_string);
  Router::PathMatcherSharedPtr matcher = createMatcherFromYaml(match_yaml_string);

  EXPECT_OK(rewriter->isCompatiblePathMatcher(matcher));
}

TEST(RewriteTest, MatchPatternInactive) {
  const std::string rewrite_yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/foo/{lang}/{country}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(rewrite_yaml_string);

  absl::Status error = rewriter->isCompatiblePathMatcher(nullptr);
  EXPECT_THAT(error, HasStatusMessage(
                         "unable to use envoy.path.rewrite.uri_template.uri_template_rewriter "
                         "extension without envoy.path.match.uri_template.uri_template_matcher "
                         "extension"));
}

TEST(RewriteTest, MatchPatternMismatchedVars) {
  const std::string rewrite_yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/foo/{lang}/{missing}"
)EOF";

  const std::string match_yaml_string = R"EOF(
      name: envoy.path.match.uri_template.uri_template_matcher
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(rewrite_yaml_string);
  Router::PathMatcherSharedPtr matcher = createMatcherFromYaml(match_yaml_string);

  absl::Status error = rewriter->isCompatiblePathMatcher(matcher);
  EXPECT_THAT(error, HasStatusMessage(
                         "mismatch between variables in path_match_policy "
                         "/bar/{lang}/{country} and path_rewrite_policy /foo/{lang}/{missing}"));
}

TEST(RewriteTest, MixedVariableLiteralRewrite) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.rewrite.uri_template.uri_template_rewriter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig
        path_template_rewrite: "/{version}/{id}"
)EOF";

  Router::PathRewriterSharedPtr rewriter = createRewriterFromYaml(yaml_string);
  // Match pattern has prefix 'v' and suffix '.json' around variables;
  // the rewrite should use only the captured values, not the surrounding literals.
  EXPECT_EQ(
      rewriter->rewritePath("/api/v2/users/456.json", "/api/v{version}/users/{id}.json").value(),
      "/2/456");
}

} // namespace Rewrite
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
