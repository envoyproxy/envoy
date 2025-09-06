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

// Test creating filter with cel_config to ensure line 38 of config.cc is covered.
// This specifically tests the path where cel_config.has_cel_config() returns true.
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

  // This call will exercise line 38 where cel_config.has_cel_config() is true
  // and getBuilder is called with the cel_config
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

} // namespace
} // namespace CEL
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
