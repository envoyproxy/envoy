#include "envoy/extensions/matching/input_matchers/uri_template/v3/uri_template.pb.h"

#include "source/extensions/matching/input_matchers/uri_template/config.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace UriTemplate {

using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::MatchResult;

class UriTemplateInputMatcherFactoryTest : public testing::Test {
protected:
  Envoy::Matcher::InputMatcherFactoryCb createMatcherFactoryCb(absl::string_view path_template) {
    envoy::extensions::matching::input_matchers::uri_template::v3::UriTemplate config;
    config.set_path_template(path_template);
    EXPECT_CALL(context_, messageValidationVisitor());
    return factory_.createInputMatcherFactoryCb(config, context_);
  }

  UriTemplateInputMatcherFactory factory_;
  testing::StrictMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(UriTemplateInputMatcherFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.matching.matchers.uri_template", factory_.name());
}

TEST_F(UriTemplateInputMatcherFactoryTest, RepeatedFactoryCallsCreateWorkingMatchers) {
  const auto matcher_factory_cb = createMatcherFactoryCb("/api/{version}/items/{id}");
  const auto first_matcher = matcher_factory_cb();
  const auto second_matcher = matcher_factory_cb();
  EXPECT_EQ(first_matcher->match(DataInputGetResult::CreateStringView("/api/v1/items/123")),
            MatchResult::Matched);
  EXPECT_EQ(second_matcher->match(DataInputGetResult::CreateStringView("/api/v1/items/123")),
            MatchResult::Matched);
  EXPECT_EQ(second_matcher->match(DataInputGetResult::CreateStringView("/api/v1/items")),
            MatchResult::NoMatch);
}

TEST_F(UriTemplateInputMatcherFactoryTest, InvalidTemplateIsRejected) {
  EXPECT_THROW_WITH_MESSAGE(createMatcherFactoryCb("/api/{version}/items/{id"), EnvoyException,
                            "path_template /api/{version}/items/{id is invalid: Unmatched "
                            "variable bracket in mixed pattern: \"{id\"");
}

TEST_F(UriTemplateInputMatcherFactoryTest, InvalidGeneratedRegexIsRejected) {
  EXPECT_THROW_WITH_MESSAGE(createMatcherFactoryCb("/a**c"), EnvoyException,
                            "path_template /a**c is invalid: bad repetition operator: **");
}

TEST_F(UriTemplateInputMatcherFactoryTest, EmptyTemplateIsRejected) {
  EXPECT_THROW_WITH_REGEX(createMatcherFactoryCb(""), EnvoyException,
                          "Proto constraint validation failed.*PathTemplate");
}

} // namespace UriTemplate
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
