#include <string>

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/http/utility.h"
#include "source/common/router/config_utility.h"
#include "source/extensions/filters/http/router/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {
namespace {

class QueryParameterMatcherTest : public testing::Test {
protected:
  QueryParameterMatcherTest() : api_(Api::createApiForTest()) {}

  Router::ConfigUtility::QueryParameterMatcher createQueryParamMatcher(const std::string& yaml) {
    envoy::config::route::v3::QueryParameterMatcher query_param_matcher;
    TestUtility::loadFromYaml(yaml, query_param_matcher);
    return Router::ConfigUtility::QueryParameterMatcher(query_param_matcher,
                                                        context_.serverFactoryContext());
  }

  Api::ApiPtr api_;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(QueryParameterMatcherTest, PresentMatchTrue) {
  const std::string yaml = R"EOF(
name: debug
present_match: true
)EOF";

  auto matcher = createQueryParamMatcher(yaml);

  // Pass the full query string including the question mark
  auto params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=1");
  EXPECT_TRUE(matcher.matches(params));

  // Test parameter exists with empty value
  auto empty_value_params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=");
  EXPECT_TRUE(matcher.matches(empty_value_params));

  // Test parameter doesn't exist
  auto no_match_params = Http::Utility::QueryParamsMulti::parseQueryString("?other=1");
  EXPECT_FALSE(matcher.matches(no_match_params));
}

TEST_F(QueryParameterMatcherTest, PresentMatchFalse) {
  const std::string yaml = R"EOF(
name: debug
present_match: false
)EOF";

  auto matcher = createQueryParamMatcher(yaml);

  // Pass the full query string including the question mark
  auto params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=1");
  EXPECT_FALSE(matcher.matches(params));

  // Test parameter exists with empty value
  auto empty_value_params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=");
  EXPECT_FALSE(matcher.matches(empty_value_params));

  // Test parameter doesn't exist
  auto no_match_params = Http::Utility::QueryParamsMulti::parseQueryString("?other=1");
  EXPECT_TRUE(matcher.matches(no_match_params));
}

TEST_F(QueryParameterMatcherTest, StringMatchWithValue) {
  const std::string yaml = R"EOF(
name: debug
string_match:
  exact: "1"
)EOF";

  auto matcher = createQueryParamMatcher(yaml);

  // Test exact match
  auto params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=1");
  EXPECT_TRUE(matcher.matches(params));

  // Test no match
  auto no_match_params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=2");
  EXPECT_FALSE(matcher.matches(no_match_params));

  // Test parameter missing
  auto missing_params = Http::Utility::QueryParamsMulti::parseQueryString("?other=1");
  EXPECT_FALSE(matcher.matches(missing_params));
}

TEST_F(QueryParameterMatcherTest, NoMatcherSpecified) {
  const std::string yaml = R"EOF(
name: debug
)EOF";

  auto matcher = createQueryParamMatcher(yaml);

  // Test parameter exists
  auto params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=1");
  EXPECT_TRUE(matcher.matches(params));

  // Test parameter exists with empty value
  auto empty_value_params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=");
  EXPECT_TRUE(matcher.matches(empty_value_params));

  // Test parameter doesn't exist
  auto no_match_params = Http::Utility::QueryParamsMulti::parseQueryString("?other=1");
  EXPECT_FALSE(matcher.matches(no_match_params));
}

TEST_F(QueryParameterMatcherTest, MultipleValues) {
  const std::string yaml = R"EOF(
name: debug
string_match:
  exact: "1"
)EOF";

  auto matcher = createQueryParamMatcher(yaml);

  // Test first value matches
  auto params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=1&debug=2");
  EXPECT_TRUE(matcher.matches(params));

  // Test second value matches but first doesn't
  auto second_match_params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=2&debug=1");
  EXPECT_FALSE(matcher.matches(second_match_params));
}

TEST_F(QueryParameterMatcherTest, PresentMatchTrueWithFeatureEnabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_new_query_param_present_match_behavior", "true"}});

  const std::string yaml = R"EOF(
name: debug
present_match: true
)EOF";

  auto matcher = createQueryParamMatcher(yaml);

  auto params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=1");
  EXPECT_TRUE(matcher.matches(params));

  auto empty_value_params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=");
  EXPECT_TRUE(matcher.matches(empty_value_params));

  auto no_match_params = Http::Utility::QueryParamsMulti::parseQueryString("?other=1");
  EXPECT_FALSE(matcher.matches(no_match_params));
}

TEST_F(QueryParameterMatcherTest, PresentMatchTrueWithFeatureDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_new_query_param_present_match_behavior", "false"}});

  const std::string yaml = R"EOF(
name: debug
present_match: true
)EOF";

  auto matcher = createQueryParamMatcher(yaml);

  // When feature is disabled, falls back to normal present check behavior
  auto params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=1");
  EXPECT_TRUE(matcher.matches(params)); // Param exists, no string matcher -> true

  auto empty_value_params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=");
  EXPECT_TRUE(matcher.matches(empty_value_params)); // Param exists, no string matcher -> true

  auto no_match_params = Http::Utility::QueryParamsMulti::parseQueryString("?other=1");
  EXPECT_FALSE(matcher.matches(no_match_params)); // Param doesn't exist -> false
}

TEST_F(QueryParameterMatcherTest, PresentMatchFalseWithFeatureEnabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_new_query_param_present_match_behavior", "true"}});

  const std::string yaml = R"EOF(
name: debug
present_match: false
)EOF";

  auto matcher = createQueryParamMatcher(yaml);

  auto params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=1");
  EXPECT_FALSE(matcher.matches(params));

  auto empty_value_params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=");
  EXPECT_FALSE(matcher.matches(empty_value_params));

  auto no_match_params = Http::Utility::QueryParamsMulti::parseQueryString("?other=1");
  EXPECT_TRUE(matcher.matches(no_match_params));
}

TEST_F(QueryParameterMatcherTest, PresentMatchFalseWithFeatureDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_new_query_param_present_match_behavior", "false"}});

  const std::string yaml = R"EOF(
name: debug
present_match: false
)EOF";

  auto matcher = createQueryParamMatcher(yaml);

  // When feature is disabled, falls back to normal present check behavior
  auto params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=1");
  EXPECT_TRUE(matcher.matches(params)); // Param exists, no string matcher -> true

  auto empty_value_params = Http::Utility::QueryParamsMulti::parseQueryString("?debug=");
  EXPECT_TRUE(matcher.matches(empty_value_params)); // Param exists, no string matcher -> true

  auto no_match_params = Http::Utility::QueryParamsMulti::parseQueryString("?other=1");
  EXPECT_FALSE(matcher.matches(no_match_params)); // Param doesn't exist -> false
}

TEST(RouterFilterConfigTest, SimpleRouterFilterConfig) {
  const std::string yaml_string = R"EOF(
  dynamic_stats: true
  )EOF";

  envoy::extensions::filters::http::router::v3::Router proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats.", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, DEPRECATED_FEATURE_TEST(SimpleRouterFilterConfigWithChildSpan)) {
  const std::string yaml_string = R"EOF(
  dynamic_stats: true
  start_child_span: true
  )EOF";

  envoy::extensions::filters::http::router::v3::Router proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats.", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, BadRouterFilterConfig) {
  const std::string yaml_string = R"EOF(
  dynamic_stats: true
  route: {}
  )EOF";

  envoy::extensions::filters::http::router::v3::Router proto_config;
  EXPECT_THROW(TestUtility::loadFromYaml(yaml_string, proto_config), EnvoyException);
}

TEST(RouterFilterConfigTest, RouterFilterWithUnsupportedStrictHeaderCheck) {
  const std::string yaml = R"EOF(
  strict_check_headers:
  - unsupportedHeader
  )EOF";

  envoy::extensions::filters::http::router::v3::Router router_config;
  TestUtility::loadFromYaml(yaml, router_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(router_config, "stats.", context).value(),
      ProtoValidationException,
      "Proto constraint validation failed \\(RouterValidationError.StrictCheckHeaders");
}

TEST(RouterFilterConfigTest, RouterV2Filter) {
  envoy::extensions::filters::http::router::v3::Router router_config;
  router_config.mutable_dynamic_stats()->set_value(true);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(router_config, "stats.", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, RouterFilterWithEmptyProtoConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats.", context)
          .value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
