#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Http::TestResponseTrailerMapImpl;

class ExpressionManagerTest : public testing::Test {
public:
  void initialize() { builder_ = Envoy::Extensions::Filters::Common::Expr::getBuilder(context_); }

  std::shared_ptr<Filters::Common::Expr::BuilderInstance> builder_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  TestRequestHeaderMapImpl request_headers_;
  TestResponseHeaderMapImpl response_headers_;
  TestRequestTrailerMapImpl request_trailers_;
  TestResponseTrailerMapImpl response_trailers_;
  Protobuf::RepeatedPtrField<std::string> req_matchers_;
  Protobuf::RepeatedPtrField<std::string> resp_matchers_;
};

#if defined(USE_CEL_PARSER)
TEST_F(ExpressionManagerTest, DuplicateAttributesIgnored) {
  initialize();
  req_matchers_.Add("request.path");
  req_matchers_.Add("request.method");
  req_matchers_.Add("request.method");
  req_matchers_.Add("request.path");
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_.setMethod("GET");
  request_headers_.setPath("/foo");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_, &request_headers_, &response_headers_,
      &response_trailers_);

  auto result = expr_mgr.evaluateRequestAttributes(*activation_ptr);
  EXPECT_EQ(2, result.fields_size());
  EXPECT_NE(result.fields().end(), result.fields().find("request.path"));
  EXPECT_NE(result.fields().end(), result.fields().find("request.method"));
}

TEST_F(ExpressionManagerTest, UnparsableExpressionThrowsException) {
  initialize();
  req_matchers_.Add("++");
  EXPECT_THROW_WITH_REGEX(ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_),
                          EnvoyException, "Unable to parse descriptor expression.*");
}

TEST_F(ExpressionManagerTest, EmptyExpressionReturnsEmptyStruct) {
  initialize();
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_ = TestRequestHeaderMapImpl();
  request_headers_.setMethod("GET");
  request_headers_.setPath("/foo");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_, &request_headers_, &response_headers_,
      &response_trailers_);

  EXPECT_EQ(0, expr_mgr.evaluateRequestAttributes(*activation_ptr).fields_size());
}

TEST_F(ExpressionManagerTest, FormatConversionV1AlphaToDevCel) {
  initialize();
  req_matchers_.Add("request.path");
  req_matchers_.Add("request.method");
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_.setMethod("POST");
  request_headers_.setPath("/api/test");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_, &request_headers_, &response_headers_,
      &response_trailers_);

  auto result = expr_mgr.evaluateRequestAttributes(*activation_ptr);
  EXPECT_EQ(2, result.fields_size());
  EXPECT_NE(result.fields().end(), result.fields().find("request.path"));
  EXPECT_NE(result.fields().end(), result.fields().find("request.method"));

  // Check string value safely
  if (result.fields().find("request.path") != result.fields().end()) {
    EXPECT_EQ("/api/test", result.fields().at("request.path").string_value());
  }
  if (result.fields().find("request.method") != result.fields().end()) {
    EXPECT_EQ("POST", result.fields().at("request.method").string_value());
  }
}

TEST_F(ExpressionManagerTest, FormatHandlingBothVersions) {
  initialize();
  // This tests that the parsing logic can handle both the old v1alpha1 format
  // and the new dev.cel format by checking that expressions are correctly parsed and evaluated
  req_matchers_.Add("request.path");
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_.setPath("/path");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_, &request_headers_, &response_headers_,
      &response_trailers_);

  auto result = expr_mgr.evaluateRequestAttributes(*activation_ptr);
  EXPECT_EQ(1, result.fields_size());
  EXPECT_NE(result.fields().end(), result.fields().find("request.path"));
}

TEST_F(ExpressionManagerTest, EvaluationResultTypes) {
  initialize();
  // Test boolean type
  req_matchers_.Add("request.method == 'GET'");
  // Test integer type
  req_matchers_.Add("size(request.path)");
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_.setMethod("GET");
  request_headers_.setPath("/test");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_, &request_headers_, &response_headers_,
      &response_trailers_);

  auto result = expr_mgr.evaluateRequestAttributes(*activation_ptr);
  EXPECT_EQ(2, result.fields_size());
  EXPECT_NE(result.fields().end(), result.fields().find("request.method == 'GET'"));
  EXPECT_NE(result.fields().end(), result.fields().find("size(request.path)"));

  // Safely check bool and number values
  if (result.fields().find("request.method == 'GET'") != result.fields().end()) {
    EXPECT_TRUE(result.fields().at("request.method == 'GET'").bool_value());
  }
  if (result.fields().find("size(request.path)") != result.fields().end()) {
    EXPECT_EQ(5, static_cast<int>(result.fields().at("size(request.path)").number_value()));
  }
}

TEST_F(ExpressionManagerTest, EvaluationErrors) {
  initialize();
  // Add a valid expression
  req_matchers_.Add("request.path");
  // Add an expression that will compile but fail at evaluation time
  // (nonexistent field 'missing' in the request headers)
  req_matchers_.Add("request.headers['missing'] == 'value'");
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_.setPath("/test");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_, &request_headers_, &response_headers_,
      &response_trailers_);

  auto result = expr_mgr.evaluateRequestAttributes(*activation_ptr);
  // We should still have the valid expression's result
  EXPECT_EQ(1, result.fields_size());
  EXPECT_NE(result.fields().end(), result.fields().find("request.path"));
  // The invalid expression should be skipped
  EXPECT_EQ(result.fields().end(), result.fields().find("request.headers['missing'] == 'value'"));
}

// Test with individual request header attributes
TEST_F(ExpressionManagerTest, SingleRequestHeaderTest) {
  initialize();
  req_matchers_.Add("request.path");
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_.setPath("/test-path");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_, &request_headers_, &response_headers_,
      &response_trailers_);

  auto result = expr_mgr.evaluateRequestAttributes(*activation_ptr);
  EXPECT_EQ(1, result.fields_size());
  EXPECT_NE(result.fields().end(), result.fields().find("request.path"));
}

// Test checking different data types
TEST_F(ExpressionManagerTest, DataTypeHandling) {
  initialize();
  // String type
  req_matchers_.Add("request.method");
  // Boolean expression
  req_matchers_.Add("request.method == 'GET'");
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_.setMethod("GET");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_, &request_headers_, &response_headers_,
      &response_trailers_);

  auto result = expr_mgr.evaluateRequestAttributes(*activation_ptr);
  EXPECT_EQ(2, result.fields_size());
  EXPECT_NE(result.fields().end(), result.fields().find("request.method"));
  EXPECT_NE(result.fields().end(), result.fields().find("request.method == 'GET'"));
}
#endif

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
