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
#endif

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
