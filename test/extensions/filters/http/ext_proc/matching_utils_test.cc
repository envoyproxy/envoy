#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

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

#ifdef USE_CEL_PARSER

class ExpressionManagerTest : public testing::Test {
protected:
  ExpressionManagerTest() {
    auto builder = Filters::Common::Expr::getBuilder(context_);
    Protobuf::RepeatedPtrField<std::string> request_matchers;
    Protobuf::RepeatedPtrField<std::string> response_matchers;
    expression_manager_ = std::make_unique<ExpressionManager>(builder, context_.local_info_,
                                                              request_matchers, response_matchers);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::unique_ptr<ExpressionManager> expression_manager_;
};

TEST_F(ExpressionManagerTest, SimpleExpression) {
  EXPECT_FALSE(expression_manager_->hasRequestExpr());
  EXPECT_FALSE(expression_manager_->hasResponseExpr());
}

TEST_F(ExpressionManagerTest, InvalidExpression) {
  Protobuf::RepeatedPtrField<std::string> request_matchers;
  request_matchers.Add("undefined_func()");
  auto builder = Filters::Common::Expr::getBuilder(context_);
  EXPECT_THROW(
      { ExpressionManager test_manager(builder, context_.local_info_, request_matchers, {}); },
      EnvoyException);
}

TEST_F(ExpressionManagerTest, RepeatedMatchers) {
  Protobuf::RepeatedPtrField<std::string> request_matchers;
  request_matchers.Add("true");
  request_matchers.Add("true");
  auto builder = Filters::Common::Expr::getBuilder(context_);
  ExpressionManager test_manager(builder, context_.local_info_, request_matchers, {});
  EXPECT_TRUE(test_manager.hasRequestExpr());
}

TEST_F(ExpressionManagerTest, EvaluateAttributesTypes) {
  Protobuf::RepeatedPtrField<std::string> matchers;
  matchers.Add("true");
  matchers.Add("1");
  matchers.Add("2u");
  matchers.Add("'foo'");
  matchers.Add("b'bar'");

  auto builder = Filters::Common::Expr::getBuilder(context_);
  ExpressionManager test_manager(builder, context_.local_info_, matchers, {});

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  auto activation = Filters::Common::Expr::createActivation(&context_.local_info_, stream_info,
                                                            nullptr, nullptr, nullptr);

  auto result = test_manager.evaluateRequestAttributes(*activation);

  EXPECT_EQ(result.fields().at("true").bool_value(), true);
  EXPECT_EQ(result.fields().at("1").number_value(), 1);
  EXPECT_EQ(result.fields().at("2u").number_value(), 2);
  EXPECT_EQ(result.fields().at("'foo'").string_value(), "foo");
  EXPECT_EQ(result.fields().at("b'bar'").string_value(), "bar");
}

TEST_F(ExpressionManagerTest, EvaluateAttributesEmpty) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  auto activation = Filters::Common::Expr::createActivation(&context_.local_info_, stream_info,
                                                            nullptr, nullptr, nullptr);
  auto result = ExpressionManager::evaluateAttributes(*activation, {});
  EXPECT_TRUE(result.fields().empty());
}

#else

TEST(ExpressionManagerTest, CelUnavailableTest) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto builder = Filters::Common::Expr::getBuilder(context);
  Protobuf::RepeatedPtrField<std::string> request_matchers;
  request_matchers.Add("true");

  // When CEL is not available, this should log a warning but not throw
  ExpressionManager manager(builder, context.local_info_, request_matchers, {});
  EXPECT_FALSE(manager.hasRequestExpr());
}

#endif // USE_CEL_PARSER

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
