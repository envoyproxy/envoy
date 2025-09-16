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

// Test creating a filter with cel_config that only enables string conversion.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithCelConfigStringConversion) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'string(response.code) == "200"'
  cel_config:
    enable_string_conversion: true
    enable_string_concat: false
    enable_string_functions: false
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  EXPECT_NE(filter, nullptr);
}

// Test creating a filter with cel_config that enables string concatenation.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithCelConfigStringConcat) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'request.headers[":method"] + "_test" == "GET_test"'
  cel_config:
    enable_string_conversion: false
    enable_string_concat: true
    enable_string_functions: false
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

// Test creating a filter with empty cel_config (all disabled).
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithEmptyCelConfig) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "response.code >= 400"
  cel_config: {}
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  EXPECT_NE(filter, nullptr);
}

// Test creating a filter with partial cel_config.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithPartialCelConfig) {
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
  EXPECT_NE(filter, nullptr);
}

// Test that createEmptyConfigProto returns the correct type.
TEST_F(CELAccessLogFilterConfigTest, CreateEmptyConfigProto) {
  auto empty_config = factory_.createEmptyConfigProto();
  EXPECT_NE(empty_config, nullptr);

  auto* typed_config =
      dynamic_cast<envoy::extensions::access_loggers::filters::cel::v3::ExpressionFilter*>(
          empty_config.get());
  EXPECT_NE(typed_config, nullptr);
}

// Test factory name.
TEST_F(CELAccessLogFilterConfigTest, FactoryName) {
  EXPECT_EQ(factory_.name(), "envoy.access_loggers.extension_filters.cel");
}

// Test creating a filter with cel_config that uses string functions in complex expression.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithComplexStringExpression) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: |
    request.headers[":path"].split("/")[1] == "api" &&
    request.headers[":method"].upperAscii() == "POST" &&
    string(response.code).startsWith("2")
  cel_config:
    enable_string_conversion: true
    enable_string_concat: false
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

// Test filter evaluation with expression that returns non-bool value.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationNonBoolResult) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "response.code"  # Returns int, not bool
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.response_code_ = 200;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  // Non-bool result should return false.
  EXPECT_FALSE(filter->evaluate(log_context, stream_info));
}

// Test filter evaluation with expression that causes runtime error.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationRuntimeError) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'request.headers["missing-header"] == "value"'  # Will error if header missing
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers; // No "missing-header"
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  // Runtime error should return false.
  EXPECT_FALSE(filter->evaluate(log_context, stream_info));
}

// Test filter evaluation with header matching.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationHeaderMatch) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'request.headers[":method"] == "GET"'
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  // Test with matching method.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}};
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_TRUE(filter->evaluate(log_context, stream_info));
  }

  // Test with non-matching method.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"}};
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

// Test filter evaluation with response headers.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationResponseHeaders) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'response.headers["content-type"] == "application/json"'
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/json"}};
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  EXPECT_TRUE(filter->evaluate(log_context, stream_info));
}

// Test filter evaluation with response trailers.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationResponseTrailers) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'response.trailers["grpc-status"] == "0"'
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  EXPECT_TRUE(filter->evaluate(log_context, stream_info));
}

TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithCelConfigCoverageTest) {
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "response.code == 200"
  cel_config:
    enable_string_conversion: false
    enable_string_concat: false
    enable_string_functions: false
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  // Also test evaluation to ensure the filter works correctly
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.response_code_ = 200;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  EXPECT_TRUE(filter->evaluate(log_context, stream_info));
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

TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithCelConfigLineCoverage) {
#if defined(USE_CEL_PARSER)
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "response.code >= 500"
  cel_config:
    enable_string_conversion: true
    enable_string_concat: true
    enable_string_functions: true
    enable_constant_folding: true
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  // Test that the filter works with all configurations enabled.
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

// Test filter creation with cel_config using direct proto construction.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterDirectProtoCelConfig) {
#if defined(USE_CEL_PARSER)
  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  proto_config.set_name("cel");

  auto* typed_config = proto_config.mutable_typed_config();
  typed_config->set_type_url(
      "type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter");

  // Create the ExpressionFilter proto directly to ensure cel_config is properly set.
  envoy::extensions::access_loggers::filters::cel::v3::ExpressionFilter expression_filter;
  expression_filter.set_expression("response.code >= 400");

  // Set cel_config to test the configuration branch.
  auto* cel_config = expression_filter.mutable_cel_config();
  cel_config->set_enable_string_conversion(true);
  cel_config->set_enable_string_concat(false);
  cel_config->set_enable_string_functions(false);
  cel_config->set_enable_constant_folding(false);

  expression_filter.SerializeToString(typed_config->mutable_value());

  // Test filter creation with cel_config present.
  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  // Verify functionality.
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.response_code_ = 404;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  EXPECT_TRUE(filter->evaluate(log_context, stream_info));
#endif
}

// Test expression compilation failure in CEL filter constructor.
// This test creates an expression that will compile successfully at parse time
// but fail during expression compilation due to an invalid operation.
TEST_F(CELAccessLogFilterConfigTest, FilterConstructorCompilationFailure) {
#if defined(USE_CEL_PARSER)
  // Create a mock factory context that will cause expression compilation to fail.
  NiceMock<Server::Configuration::MockFactoryContext> mock_context;
  NiceMock<LocalInfo::MockLocalInfo> mock_local_info;
  ON_CALL(mock_context.server_factory_context_, localInfo())
      .WillByDefault(ReturnRef(mock_local_info));

  // This expression should parse but fail during compilation due to type mismatch.
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'true + false'  # Invalid: cannot add booleans
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // This should throw during filter creation due to expression compilation failure.
  EXPECT_THROW_WITH_MESSAGE(factory_.createFilter(proto_config, mock_context), EnvoyException,
                            "failed to create an expression:");
#endif
}

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

// Test filter creation with constant folding enabled in cel_config.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithConstantFolding) {
#if defined(USE_CEL_PARSER)
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "response.code >= 400"
  cel_config:
    enable_constant_folding: true
)EOF";

  envoy::config::accesslog::v3::ExtensionFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = factory_.createFilter(proto_config, context_);
  ASSERT_NE(filter, nullptr);

  // Test that the filter works correctly with constant folding enabled.
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.response_code_ = 404;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  EXPECT_TRUE(filter->evaluate(log_context, stream_info));
#endif
}

// Test expression evaluation with non-boolean result.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationNonBoolNonErrorResult) {
#if defined(USE_CEL_PARSER)
  const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: '"hello world"'  # Returns string, not bool or error
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

  // Non-boolean expressions should return false for filtering.
  EXPECT_FALSE(filter->evaluate(log_context, stream_info));
#endif
}

// Test various cel_config combinations to ensure all config paths are covered.
TEST_F(CELAccessLogFilterConfigTest, CreateFilterWithVariousCelConfigs) {
#if defined(USE_CEL_PARSER)
  // Test with only enable_constant_folding set.
  {
    const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "response.code == 200"
  cel_config:
    enable_constant_folding: true
    enable_string_conversion: false
    enable_string_concat: false
    enable_string_functions: false
)EOF";

    envoy::config::accesslog::v3::ExtensionFilter proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    auto filter = factory_.createFilter(proto_config, context_);
    EXPECT_NE(filter, nullptr);
  }

  // Test with mixed configuration.
  {
    const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'string(response.code) + "_test"'
  cel_config:
    enable_constant_folding: false
    enable_string_conversion: true
    enable_string_concat: true
    enable_string_functions: false
)EOF";

    envoy::config::accesslog::v3::ExtensionFilter proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    auto filter = factory_.createFilter(proto_config, context_);
    EXPECT_NE(filter, nullptr);
  }
#endif
}

// Test additional edge cases in expression evaluation to improve coverage.
TEST_F(CELAccessLogFilterConfigTest, FilterEvaluationEdgeCases) {
#if defined(USE_CEL_PARSER)
  // Test with null/missing response code.
  {
    const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: "has(response.code) && response.code >= 400"
)EOF";

    envoy::config::accesslog::v3::ExtensionFilter proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    auto filter = factory_.createFilter(proto_config, context_);
    ASSERT_NE(filter, nullptr);

    // Test with stream info that has no response code set.
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    // Don't set response_code_ to test the has() condition
    Http::TestRequestHeaderMapImpl request_headers;
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_FALSE(filter->evaluate(log_context, stream_info));
  }

  // Test with expression that can return null.
  {
    const std::string yaml = R"EOF(
name: cel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.cel.v3.ExpressionFilter
  expression: 'request.headers["x-custom-header"].size() > 0'
)EOF";

    envoy::config::accesslog::v3::ExtensionFilter proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    auto filter = factory_.createFilter(proto_config, context_);
    ASSERT_NE(filter, nullptr);

    // Test without the header - should handle gracefully.
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    Http::TestRequestHeaderMapImpl request_headers; // No x-custom-header
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_FALSE(filter->evaluate(log_context, stream_info));
  }
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
  cel_config->set_enable_constant_folding(true);

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
