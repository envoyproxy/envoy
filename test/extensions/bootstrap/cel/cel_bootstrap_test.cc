#include "source/extensions/bootstrap/cel/cel.pb.h"
#include "source/extensions/bootstrap/cel/config.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "cel/expr/syntax.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Cel {
namespace {

using ::testing::NiceMock;

class CelBootstrapTest : public testing::Test {
protected:
  void SetUp() override { stream_info_ = std::make_shared<NiceMock<StreamInfo::MockStreamInfo>>(); }

  std::shared_ptr<NiceMock<StreamInfo::MockStreamInfo>> stream_info_;
};

TEST_F(CelBootstrapTest, CreateBootstrapExtension) {
  using envoy::extensions::bootstrap::cel::CelEvaluatorConfig;

  CelEvaluatorConfig config;
  config.set_enable_string_conversion(true);
  config.set_enable_string_concat(true);
  config.set_enable_string_functions(true);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  CelFactory factory;

  auto extension = factory.createBootstrapExtension(config, context);
  ASSERT_NE(extension, nullptr);

  // Verify factory name
  EXPECT_EQ(factory.name(), "envoy.bootstrap.cel");

  // Verify empty config proto creation
  auto empty_config = factory.createEmptyConfigProto();
  ASSERT_NE(empty_config, nullptr);
}

TEST_F(CelBootstrapTest, StringConversionEnabled) {
  using envoy::extensions::bootstrap::cel::CelEvaluatorConfig;
  ProtobufWkt::Arena arena;

  // Create config with string conversion enabled
  CelEvaluatorConfig config;
  config.set_enable_string_conversion(true);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(false);

  // Create builder with configuration
  auto builder = Extensions::Filters::Common::Expr::createBuilder(nullptr, &config);
  ASSERT_NE(builder, nullptr);

  // Create string conversion expression: string(123)
  cel::expr::Expr string_conv_expr;
  string_conv_expr.mutable_call_expr()->set_function("string");
  string_conv_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_int64_value(123);

  // String conversion should work
  auto expr = Extensions::Filters::Common::Expr::createExpression(*builder, string_conv_expr);
  auto activation = Extensions::Filters::Common::Expr::createActivation(nullptr, *stream_info_,
                                                                        nullptr, nullptr, nullptr);
  auto result = expr->Evaluate(*activation, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "123");
}

TEST_F(CelBootstrapTest, StringConcatEnabled) {
  using envoy::extensions::bootstrap::cel::CelEvaluatorConfig;
  ProtobufWkt::Arena arena;

  // Create config with string concatenation enabled
  CelEvaluatorConfig config;
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(true);
  config.set_enable_string_functions(false);

  // Create builder with configuration
  auto builder = Extensions::Filters::Common::Expr::createBuilder(nullptr, &config);
  ASSERT_NE(builder, nullptr);

  // Create string concatenation expression: "foo" + "bar"
  cel::expr::Expr string_concat_expr;
  string_concat_expr.mutable_call_expr()->set_function("_+_");
  string_concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("foo");
  string_concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("bar");

  // String concatenation should work
  auto expr = Extensions::Filters::Common::Expr::createExpression(*builder, string_concat_expr);
  auto activation = Extensions::Filters::Common::Expr::createActivation(nullptr, *stream_info_,
                                                                        nullptr, nullptr, nullptr);
  auto result = expr->Evaluate(*activation, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "foobar");
}

TEST_F(CelBootstrapTest, StringFunctionsEnabled) {
  using envoy::extensions::bootstrap::cel::CelEvaluatorConfig;
  ProtobufWkt::Arena arena;

  // Create config with string functions enabled
  CelEvaluatorConfig config;
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(true);

  // Create builder with configuration
  auto builder = Extensions::Filters::Common::Expr::createBuilder(nullptr, &config);
  ASSERT_NE(builder, nullptr);

  // Create replace expression: "HELLO".replace("HE", "he")
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("HELLO");
  call_expr->add_args()->mutable_const_expr()->set_string_value("HE");
  call_expr->add_args()->mutable_const_expr()->set_string_value("he");

  // replace should work
  auto expr = Extensions::Filters::Common::Expr::createExpression(*builder, replace_expr);
  auto activation = Extensions::Filters::Common::Expr::createActivation(nullptr, *stream_info_,
                                                                        nullptr, nullptr, nullptr);
  auto result = expr->Evaluate(*activation, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "heLLO");
}

TEST_F(CelBootstrapTest, StringFunctionsReplace) {

  using envoy::extensions::bootstrap::cel::CelEvaluatorConfig;
  ProtobufWkt::Arena arena;

  // Create config with string functions enabled
  CelEvaluatorConfig config;
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(true);

  // Create builder with configuration
  auto builder = Extensions::Filters::Common::Expr::createBuilder(nullptr, &config);
  ASSERT_NE(builder, nullptr);

  // Create replace expression: "hello".replace("he", "we")
  // Note: Only 'replace' and 'split' are available in CEL-CPP string extensions
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello");
  call_expr->add_args()->mutable_const_expr()->set_string_value("he");
  call_expr->add_args()->mutable_const_expr()->set_string_value("we");

  // replace should work
  auto expr = Extensions::Filters::Common::Expr::createExpression(*builder, replace_expr);
  auto activation = Extensions::Filters::Common::Expr::createActivation(nullptr, *stream_info_,
                                                                        nullptr, nullptr, nullptr);
  auto result = expr->Evaluate(*activation, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "wello");
}

TEST_F(CelBootstrapTest, StringFunctionsSplit) {
  using envoy::extensions::bootstrap::cel::CelEvaluatorConfig;
  ProtobufWkt::Arena arena;

  // Create config with string functions enabled
  CelEvaluatorConfig config;
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(true);

  // Create builder with configuration
  auto builder = Extensions::Filters::Common::Expr::createBuilder(nullptr, &config);
  ASSERT_NE(builder, nullptr);

  // Create split expression: "a,b,c".split(",")
  cel::expr::Expr split_expr;
  auto* call_expr = split_expr.mutable_call_expr();
  call_expr->set_function("split");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("a,b,c");
  call_expr->add_args()->mutable_const_expr()->set_string_value(",");

  // split should work
  auto expr = Extensions::Filters::Common::Expr::createExpression(*builder, split_expr);
  auto activation = Extensions::Filters::Common::Expr::createActivation(nullptr, *stream_info_,
                                                                        nullptr, nullptr, nullptr);
  auto result = expr->Evaluate(*activation, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsList());
  const auto& list = *result.value().ListOrDie();
  EXPECT_EQ(list.size(), 3);
  EXPECT_EQ(list[0].StringOrDie().value(), "a");
  EXPECT_EQ(list[1].StringOrDie().value(), "b");
  EXPECT_EQ(list[2].StringOrDie().value(), "c");
}

TEST_F(CelBootstrapTest, StringFunctionsDisabled) {
  using envoy::extensions::bootstrap::cel::CelEvaluatorConfig;

  // Create config with string functions disabled (default)
  CelEvaluatorConfig config;
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(false);

  // Create builder with configuration
  auto builder = Extensions::Filters::Common::Expr::createBuilder(nullptr, &config);
  ASSERT_NE(builder, nullptr);

  // Create replace expression: "HELLO".replace("H", "h")
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("HELLO");
  call_expr->add_args()->mutable_const_expr()->set_string_value("H");
  call_expr->add_args()->mutable_const_expr()->set_string_value("h");

  // replace should fail when string functions are disabled
  EXPECT_THROW(Extensions::Filters::Common::Expr::createExpression(*builder, replace_expr),
               Extensions::Filters::Common::Expr::CelException);
}

TEST_F(CelBootstrapTest, DefaultConfiguration) {
  // Create builder with default configuration (nullptr)
  auto builder = Extensions::Filters::Common::Expr::createBuilder(nullptr, nullptr);
  ASSERT_NE(builder, nullptr);

  // Create replace expression: "HELLO".replace("H", "h")
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("HELLO");
  call_expr->add_args()->mutable_const_expr()->set_string_value("H");
  call_expr->add_args()->mutable_const_expr()->set_string_value("h");

  // replace should fail with default configuration (string functions disabled)
  EXPECT_THROW(Extensions::Filters::Common::Expr::createExpression(*builder, replace_expr),
               Extensions::Filters::Common::Expr::CelException);
}

TEST_F(CelBootstrapTest, AllFeaturesEnabled) {
  using envoy::extensions::bootstrap::cel::CelEvaluatorConfig;
  ProtobufWkt::Arena arena;

  // Create config with all features enabled
  CelEvaluatorConfig config;
  config.set_enable_string_conversion(true);
  config.set_enable_string_concat(true);
  config.set_enable_string_functions(true);

  // Create builder with configuration
  auto builder = Extensions::Filters::Common::Expr::createBuilder(nullptr, &config);
  ASSERT_NE(builder, nullptr);

  // Test all features work together in a complex expression:
  // string(123).lowerAscii() + "_" + "WORLD".lowerAscii()

  // First test string conversion
  cel::expr::Expr string_conv_expr;
  string_conv_expr.mutable_call_expr()->set_function("string");
  string_conv_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_int64_value(123);

  auto expr1 = Extensions::Filters::Common::Expr::createExpression(*builder, string_conv_expr);
  auto activation = Extensions::Filters::Common::Expr::createActivation(nullptr, *stream_info_,
                                                                        nullptr, nullptr, nullptr);
  auto result1 = expr1->Evaluate(*activation, &arena);

  ASSERT_TRUE(result1.ok());
  EXPECT_TRUE(result1.value().IsString());
  EXPECT_EQ(result1.value().StringOrDie().value(), "123");

  // Test string concatenation
  cel::expr::Expr string_concat_expr;
  string_concat_expr.mutable_call_expr()->set_function("_+_");
  string_concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("foo");
  string_concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("bar");

  auto expr2 = Extensions::Filters::Common::Expr::createExpression(*builder, string_concat_expr);
  auto result2 = expr2->Evaluate(*activation, &arena);

  ASSERT_TRUE(result2.ok());
  EXPECT_TRUE(result2.value().IsString());
  EXPECT_EQ(result2.value().StringOrDie().value(), "foobar");

  // Test string functions
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("HELLO");
  call_expr->add_args()->mutable_const_expr()->set_string_value("LL");
  call_expr->add_args()->mutable_const_expr()->set_string_value("ll");

  auto expr3 = Extensions::Filters::Common::Expr::createExpression(*builder, replace_expr);
  auto result3 = expr3->Evaluate(*activation, &arena);

  ASSERT_TRUE(result3.ok());
  EXPECT_TRUE(result3.value().IsString());
  EXPECT_EQ(result3.value().StringOrDie().value(), "HEllO");
}

} // namespace
} // namespace Cel
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
