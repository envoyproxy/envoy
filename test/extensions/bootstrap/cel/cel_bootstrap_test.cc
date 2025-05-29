#include "source/extensions/bootstrap/cel/config.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "google/api/expr/v1alpha1/syntax.pb.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Cel {
namespace {

TEST(CelBootstrapTest, StringConversionAndConcatConfig) {
  using envoy::extensions::filters::common::expr::v3::CelEvaluatorConfig;
  ProtobufWkt::Arena arena;

  // Expression: string(123)
  auto string_conv_expr = std::make_unique<google::api::expr::v1alpha1::Expr>();
  string_conv_expr->mutable_call_expr()->set_function("string");
  string_conv_expr->mutable_call_expr()->add_args()->mutable_const_expr()->set_int64_value(123);

  // Expression: "foo" + "bar"
  auto string_concat_expr = std::make_unique<google::api::expr::v1alpha1::Expr>();
  string_concat_expr->mutable_call_expr()->set_function("_+_");
  string_concat_expr->mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("foo");
  string_concat_expr->mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("bar");

  // Test both disabled (default)
  {
    auto config = std::make_unique<CelEvaluatorConfig>();
    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    auto stream_info = std::make_shared<NiceMock<StreamInfo::MockStreamInfo>>();
    ON_CALL(context, streamInfo()).WillByDefault(ReturnRef(*stream_info));
    CelFactory factory;
    auto extension = factory.createBootstrapExtension(*config, context);
    ASSERT_NE(extension, nullptr);

    auto builder = Extensions::Filters::Common::Expr::getBuilder(context);
    ASSERT_NE(builder, nullptr);

    // String conversion should fail
    EXPECT_THROW(Extensions::Filters::Common::Expr::createExpression(builder->builder(), *string_conv_expr), 
                 Filters::Common::Expr::CelException);
    // String concat should fail
    EXPECT_THROW(Extensions::Filters::Common::Expr::createExpression(builder->builder(), *string_concat_expr), 
                 Filters::Common::Expr::CelException);
  }

  // Test string conversion enabled, concat disabled
  {
    auto config = std::make_unique<CelEvaluatorConfig>();
    config->set_enable_string_conversion(true);
    config->set_enable_string_concat(false);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    auto stream_info = std::make_shared<NiceMock<StreamInfo::MockStreamInfo>>();
    ON_CALL(context, streamInfo()).WillByDefault(ReturnRef(*stream_info));
    CelFactory factory;
    auto extension = factory.createBootstrapExtension(*config, context);
    ASSERT_NE(extension, nullptr);

    auto builder = Extensions::Filters::Common::Expr::getBuilder(context);
    ASSERT_NE(builder, nullptr);

    // String conversion should work
    auto expr_conv = Extensions::Filters::Common::Expr::createExpression(builder->builder(), *string_conv_expr);
    auto activation = Extensions::Filters::Common::Expr::createActivation(nullptr, *stream_info, nullptr, nullptr, nullptr);
    auto result_conv = expr_conv->Evaluate(*activation, &arena);
    ASSERT_TRUE(result_conv.ok());
    EXPECT_TRUE(result_conv.value().IsString());
    EXPECT_EQ(result_conv.value().StringOrDie().value(), "123");

    // String concat should fail
    EXPECT_THROW(Extensions::Filters::Common::Expr::createExpression(builder->builder(), *string_concat_expr), 
                 Filters::Common::Expr::CelException);
  }

  // Test string conversion disabled, concat enabled
  {
    auto config = std::make_unique<CelEvaluatorConfig>();
    config->set_enable_string_conversion(false);
    config->set_enable_string_concat(true);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    auto stream_info = std::make_shared<NiceMock<StreamInfo::MockStreamInfo>>();
    ON_CALL(context, streamInfo()).WillByDefault(ReturnRef(*stream_info));
    CelFactory factory;
    auto extension = factory.createBootstrapExtension(*config, context);
    ASSERT_NE(extension, nullptr);

    auto builder = Extensions::Filters::Common::Expr::getBuilder(context);
    ASSERT_NE(builder, nullptr);

    // String conversion should fail
    EXPECT_THROW(Extensions::Filters::Common::Expr::createExpression(builder->builder(), *string_conv_expr), 
                 Filters::Common::Expr::CelException);

    // String concat should work
    auto expr_concat = Extensions::Filters::Common::Expr::createExpression(builder->builder(), *string_concat_expr);
    auto activation = Extensions::Filters::Common::Expr::createActivation(nullptr, *stream_info, nullptr, nullptr, nullptr);
    auto result_concat = expr_concat->Evaluate(*activation, &arena);
    ASSERT_TRUE(result_concat.ok());
    EXPECT_TRUE(result_concat.value().IsString());
    EXPECT_EQ(result_concat.value().StringOrDie().value(), "foobar");
  }

  // Test both enabled
  {
    auto config = std::make_unique<CelEvaluatorConfig>();
    config->set_enable_string_conversion(true);
    config->set_enable_string_concat(true);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    auto stream_info = std::make_shared<NiceMock<StreamInfo::MockStreamInfo>>();
    ON_CALL(context, streamInfo()).WillByDefault(ReturnRef(*stream_info));
    CelFactory factory;
    auto extension = factory.createBootstrapExtension(*config, context);
    ASSERT_NE(extension, nullptr);

    auto builder = Extensions::Filters::Common::Expr::getBuilder(context);
    ASSERT_NE(builder, nullptr);

    // String conversion should work
    auto expr_conv = Extensions::Filters::Common::Expr::createExpression(builder->builder(), *string_conv_expr);
    auto activation = Extensions::Filters::Common::Expr::createActivation(nullptr, *stream_info, nullptr, nullptr, nullptr);
    auto result_conv = expr_conv->Evaluate(*activation, &arena);
    ASSERT_TRUE(result_conv.ok());
    EXPECT_TRUE(result_conv.value().IsString());
    EXPECT_EQ(result_conv.value().StringOrDie().value(), "123");

    // String concat should work
    auto expr_concat = Extensions::Filters::Common::Expr::createExpression(builder->builder(), *string_concat_expr);
    auto result_concat = expr_concat->Evaluate(*activation, &arena);
    ASSERT_TRUE(result_concat.ok());
    EXPECT_TRUE(result_concat.value().IsString());
    EXPECT_EQ(result_concat.value().StringOrDie().value(), "foobar");
  }
}

} // namespace
} // namespace Cel
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy 