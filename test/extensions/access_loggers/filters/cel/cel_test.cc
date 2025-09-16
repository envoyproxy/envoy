#include "source/extensions/access_loggers/filters/cel/cel.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if defined(USE_CEL_PARSER)
#include "parser/parser.h"
#endif

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace CEL {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

class CELAccessLogFilterTest : public testing::Test {
protected:
  void SetUp() override {}

  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

#if defined(USE_CEL_PARSER)

// Test CELAccessLogExtensionFilter constructor with valid expression.
TEST_F(CELAccessLogFilterTest, ConstructorWithValidExpression) {
  auto parse_status = google::api::expr::parser::Parse("response.code >= 400");
  ASSERT_TRUE(parse_status.ok());

  auto builder = Extensions::Filters::Common::Expr::getBuilder(
      *testing::NiceMock<Server::Configuration::MockFactoryContext>().serverFactoryContextPtr());

  CELAccessLogExtensionFilter filter(local_info_, builder, parse_status.value().expr());
}

// Test CELAccessLogExtensionFilter constructor with invalid expression that fails compilation.
TEST_F(CELAccessLogFilterTest, ConstructorWithInvalidExpression) {
  // Create an expression that parses but fails at compilation.
  cel::expr::Expr invalid_expr;
  auto* call_expr = invalid_expr.mutable_call_expr();
  call_expr->set_function("_+_"); // Addition function
  call_expr->add_args()->mutable_const_expr()->set_bool_value(true);
  call_expr->add_args()->mutable_const_expr()->set_bool_value(false);

  auto builder = Extensions::Filters::Common::Expr::getBuilder(
      *testing::NiceMock<Server::Configuration::MockFactoryContext>().serverFactoryContextPtr());

  EXPECT_THROW_WITH_MESSAGE(CELAccessLogExtensionFilter(local_info_, builder, invalid_expr),
                            EnvoyException, "failed to create an expression:");
}

// Test evaluate method with various result scenarios.
TEST_F(CELAccessLogFilterTest, EvaluateWithDifferentResults) {
  auto parse_status = google::api::expr::parser::Parse("response.code >= 400");
  ASSERT_TRUE(parse_status.ok());

  auto builder = Extensions::Filters::Common::Expr::getBuilder(
      *testing::NiceMock<Server::Configuration::MockFactoryContext>().serverFactoryContextPtr());

  CELAccessLogExtensionFilter filter(local_info_, builder, parse_status.value().expr());

  // Test case 1: Valid boolean result (true).
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.response_code_ = 404;
    Http::TestRequestHeaderMapImpl request_headers;
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_TRUE(filter.evaluate(log_context, stream_info));
  }

  // Test case 2: Valid boolean result (false).
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.response_code_ = 200;
    Http::TestRequestHeaderMapImpl request_headers;
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_FALSE(filter.evaluate(log_context, stream_info));
  }
}

// Test evaluate method with non-boolean result.
TEST_F(CELAccessLogFilterTest, EvaluateWithNonBooleanResult) {
  auto parse_status = google::api::expr::parser::Parse("response.code");
  ASSERT_TRUE(parse_status.ok());

  auto builder = Extensions::Filters::Common::Expr::getBuilder(
      *testing::NiceMock<Server::Configuration::MockFactoryContext>().serverFactoryContextPtr());

  CELAccessLogExtensionFilter filter(local_info_, builder, parse_status.value().expr());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.response_code_ = 200;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  // Non-boolean result should return false.
  EXPECT_FALSE(filter.evaluate(log_context, stream_info));
}

// Test evaluate method with expression that produces an error.
TEST_F(CELAccessLogFilterTest, EvaluateWithErrorResult) {
  auto parse_status = google::api::expr::parser::Parse("1 / 0");
  ASSERT_TRUE(parse_status.ok());

  auto builder = Extensions::Filters::Common::Expr::getBuilder(
      *testing::NiceMock<Server::Configuration::MockFactoryContext>().serverFactoryContextPtr());

  CELAccessLogExtensionFilter filter(local_info_, builder, parse_status.value().expr());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  // Error result should return false.
  EXPECT_FALSE(filter.evaluate(log_context, stream_info));
}

// Test evaluate method with expression that has no result.
TEST_F(CELAccessLogFilterTest, EvaluateWithNoResult) {
  // Expression that might fail evaluation in certain contexts.
  auto parse_status =
      google::api::expr::parser::Parse("request.headers[\"missing-header\"] == \"value\"");
  ASSERT_TRUE(parse_status.ok());

  auto builder = Extensions::Filters::Common::Expr::getBuilder(
      *testing::NiceMock<Server::Configuration::MockFactoryContext>().serverFactoryContextPtr());

  CELAccessLogExtensionFilter filter(local_info_, builder, parse_status.value().expr());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers; // No "missing-header"
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  // Missing header should return false.
  EXPECT_FALSE(filter.evaluate(log_context, stream_info));
}

// Test evaluate method with complex expressions.
TEST_F(CELAccessLogFilterTest, EvaluateWithComplexExpressions) {
  auto parse_status = google::api::expr::parser::Parse(
      "response.code >= 400 && request.headers[\":method\"] == \"POST\"");
  ASSERT_TRUE(parse_status.ok());

  auto builder = Extensions::Filters::Common::Expr::getBuilder(
      *testing::NiceMock<Server::Configuration::MockFactoryContext>().serverFactoryContextPtr());

  CELAccessLogExtensionFilter filter(local_info_, builder, parse_status.value().expr());

  // Test both conditions true.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.response_code_ = 404;
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"}};
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_TRUE(filter.evaluate(log_context, stream_info));
  }

  // Test one condition false.
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.response_code_ = 404;
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}};
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    Formatter::HttpFormatterContext log_context{
        &request_headers, &response_headers, &response_trailers, {}};

    EXPECT_FALSE(filter.evaluate(log_context, stream_info));
  }
}

// Test evaluate method with headers, trailers, etc.
TEST_F(CELAccessLogFilterTest, EvaluateWithHeadersAndTrailers) {
  auto parse_status = google::api::expr::parser::Parse(
      "response.headers[\"content-type\"] == \"application/json\" && "
      "response.trailers[\"grpc-status\"] == \"0\"");
  ASSERT_TRUE(parse_status.ok());

  auto builder = Extensions::Filters::Common::Expr::getBuilder(
      *testing::NiceMock<Server::Configuration::MockFactoryContext>().serverFactoryContextPtr());

  CELAccessLogExtensionFilter filter(local_info_, builder, parse_status.value().expr());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/json"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  Formatter::HttpFormatterContext log_context{
      &request_headers, &response_headers, &response_trailers, {}};

  EXPECT_TRUE(filter.evaluate(log_context, stream_info));
}

#endif

} // namespace
} // namespace CEL
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
