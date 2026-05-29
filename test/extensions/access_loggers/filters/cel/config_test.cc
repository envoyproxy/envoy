#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/filters/cel/v3/cel.pb.h"

#include "source/extensions/access_loggers/filters/cel/cel.h"
#include "source/extensions/access_loggers/filters/cel/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace CEL {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class CELAccessLogFilterConfigTest : public testing::Test {
protected:
  void SetUp() override {
    ON_CALL(context_.server_factory_context_, localInfo()).WillByDefault(ReturnRef(local_info_));
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  CELAccessLogExtensionFilterFactory factory_;
};

#if defined(USE_CEL_PARSER)

// Test creating a filter without cel_config (default behavior).
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithoutCelConfig) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "response.code >= 400"
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  EXPECT_NE(filter, nullptr);
}

// Test creating a filter with cel_config that enables string functions.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithCelConfigStringFunctions) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'request.headers[":method"].lowerAscii() == "get"'
  cel_config:
    enable_string_conversion: true
    enable_string_concat: true
    enable_string_functions: true
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  EXPECT_NE(filter, nullptr);
}

// Test creating a filter with an invalid expression.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithInvalidExpression) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "this is not a valid CEL expression @#$%"
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  EXPECT_THROW_WITH_MESSAGE(factory_.createFilter(proto_config, context_), EnvoyException,
                            "Not able to parse filter expression:");
}

// Test creating a filter with all string features enabled.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithAllStringFeaturesEnabled) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'string(response.code).replace("20", "30") + "_suffix"'
  cel_config:
    enable_string_conversion: true
    enable_string_concat: true
    enable_string_functions: true
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  EXPECT_NE(filter, nullptr);
}

// Test actual filter evaluation with successful expression.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationSuccess) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "response.code >= 400"
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  // Test with response code >= 400 (should match).
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.response_code_ = 404;
    Http::TestRequestHeaderMapImpl request_headers;
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_TRUE(filter->evaluate(log_context, stream_info));
  }

  // Test with response code < 400 (should not match).
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.response_code_ = 200;
    Http::TestRequestHeaderMapImpl request_headers;
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_FALSE(filter->evaluate(log_context, stream_info));
  }
}

// Test filter evaluation with complex expression using logical operators.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationComplexExpression) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'response.code >= 400 && request.headers[":method"] == "POST"'
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  // Test with both conditions met.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.response_code_ = 404;
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"}};
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_TRUE(filter->evaluate(log_context, stream_info));
  }

  // Test with only one condition met.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.response_code_ = 404;
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}};
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_FALSE(filter->evaluate(log_context, stream_info));
  }
}

// Test creating filter with cel_config and evaluating with string functions.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationWithStringFunctions) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'request.headers[":path"].contains("/api")'
  cel_config:
    enable_string_functions: true
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  // Test with path containing /api.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/api/v1/users"}};
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_TRUE(filter->evaluate(log_context, stream_info));
  }

  // Test with path not containing /api.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/health"}};
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_FALSE(filter->evaluate(log_context, stream_info));
  }
}

#endif

// Test evaluate method with expression that returns an error value.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationWithErrorResult) {
#if defined(USE_CEL_PARSER)
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: '1 / 0'  # This should create an error during evaluation
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  // Expression that causes division by zero should return false.
  EXPECT_FALSE(filter->evaluate(log_context, stream_info));
#endif
}

// Test evaluate method with expression that has no result (returns empty optional).
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationWithNoResult) {
#if defined(USE_CEL_PARSER)
  // Create an expression that might not have a value in certain contexts.
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'request.headers["non-existent-header"] != ""'
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers; // Empty headers
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  // This should handle the case where the header doesn't exist gracefully.
  EXPECT_FALSE(filter->evaluate(log_context, stream_info));
#endif
}

// Test to ensure factory method registration path is tested.
TEST_F(CELAccessLogFilterConfigTest, FactoryRegistration) {
  // Test the factory instance.
  EXPECT_EQ(factory_.name(), "envoy.access_loggers.extension_filters.cel");

  // Test createEmptyConfigProto method.
  auto empty_config = factory_.createEmptyConfigProto();
  EXPECT_NE(empty_config, nullptr);

  auto* typed_config =
      dynamic_cast<envoy::extensions::access_loggers::filters::cel::v3::ExpressionFilter*>(
          empty_config.get());
  EXPECT_NE(typed_config, nullptr);
  EXPECT_TRUE(typed_config->expression().empty());
}

// Test filter creation with comprehensive cel_config using PackFrom.
TEST_F(CELAccessLogFilterConfigTest, ForceHitLines38_39InConfigCC) {
#if defined(USE_CEL_PARSER)
  // Build the proto configuration manually to ensure cel_config is properly set.
  envoy::extensions::access_loggers::filters::cel::v3::ExpressionFilter cel_filter_config;
  cel_filter_config.set_expression("response.code >= 400");

  // Configure cel_config with all string features enabled.
  auto* cel_config = cel_filter_config.mutable_cel_config();
  cel_config->set_enable_string_conversion(true);
  cel_config->set_enable_string_concat(true);
  cel_config->set_enable_string_functions(true);

  // Create the ExtensionFilter wrapper.
  envoy::config::accesslog::v3::ExtensionFilter extension_filter;
  extension_filter.set_name("cel");
  extension_filter.mutable_typed_config()->PackFrom(cel_filter_config);

  // Test filter creation with cel_config using PackFrom method.
  auto filter = factory_.createFilter(extension_filter, context_);
  ASSERT_NE(filter, nullptr);

  // Test functionality to ensure the filter works.
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.response_code_ = 500;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  EXPECT_TRUE(filter->evaluate(log_context, stream_info));
#endif
}

// Test expression compilation failure with invalid CEL syntax.
TEST_F(CELAccessLogFilterConfigTest, InvalidCelExpressionCompilation) {
#if defined(USE_CEL_PARSER)
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "invalid syntax @#$%^&*()"
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Should throw during filter creation due to parse failure.
  EXPECT_THROW_WITH_MESSAGE(factory_.createFilter(proto_config, context_), EnvoyException,
                            "Not able to parse filter expression:");
#endif
}

} // namespace
} // namespace CEL
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
