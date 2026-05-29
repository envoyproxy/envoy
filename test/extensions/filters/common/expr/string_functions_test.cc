#include "source/extensions/filters/common/expr/evaluator.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "cel/expr/syntax.pb.h"
#include "eval/public/structs/cel_proto_wrapper.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace {

using ::testing::_;
using ::testing::NiceMock;

class StringFunctionsTest : public testing::Test {
protected:
  void SetUp() override {
    // Create configuration with string functions enabled.
    config_.set_enable_string_conversion(true);
    config_.set_enable_string_concat(true);
    config_.set_enable_string_functions(true);

    builder_ = createBuilder(makeOptRef(config_));
    stream_info_ = std::make_unique<NiceMock<StreamInfo::MockStreamInfo>>();
    activation_ = createActivation(nullptr, *stream_info_, nullptr, nullptr, nullptr);
  }

  envoy::config::core::v3::CelExpressionConfig config_;
  BuilderPtr builder_;
  std::unique_ptr<NiceMock<StreamInfo::MockStreamInfo>> stream_info_;
  ActivationPtr activation_;
};

TEST_F(StringFunctionsTest, Replace) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello hello");
  call_expr->add_args()->mutable_const_expr()->set_string_value("he");
  call_expr->add_args()->mutable_const_expr()->set_string_value("we");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);
  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "wello wello");
}

TEST_F(StringFunctionsTest, ReplaceWithLimit) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello hello");
  call_expr->add_args()->mutable_const_expr()->set_string_value("he");
  call_expr->add_args()->mutable_const_expr()->set_string_value("we");
  call_expr->add_args()->mutable_const_expr()->set_int64_value(1);

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);
  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "wello hello");
}

TEST_F(StringFunctionsTest, Split) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("split");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello hello hello");
  call_expr->add_args()->mutable_const_expr()->set_string_value(" ");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);
  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsList());
  const auto& list = *result.value().ListOrDie();
  EXPECT_EQ(list.size(), 3);
  EXPECT_EQ(list[0].StringOrDie().value(), "hello");
  EXPECT_EQ(list[1].StringOrDie().value(), "hello");
  EXPECT_EQ(list[2].StringOrDie().value(), "hello");
}

TEST_F(StringFunctionsTest, SplitWithLimit) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("split");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello hello hello");
  call_expr->add_args()->mutable_const_expr()->set_string_value(" ");
  call_expr->add_args()->mutable_const_expr()->set_int64_value(2);

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);
  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsList());
  const auto& list = *result.value().ListOrDie();
  EXPECT_EQ(list.size(), 2);
  EXPECT_EQ(list[0].StringOrDie().value(), "hello");
  EXPECT_EQ(list[1].StringOrDie().value(), "hello hello");
}

TEST_F(StringFunctionsTest, StringFunctionsDisabled) {
  // Create configuration with string functions disabled.
  envoy::config::core::v3::CelExpressionConfig disabled_config;
  disabled_config.set_enable_string_conversion(false);
  disabled_config.set_enable_string_concat(false);
  disabled_config.set_enable_string_functions(false);

  auto disabled_builder = createBuilder(makeOptRef(disabled_config));

  // Create replace expression: "hello".replace("he", "we")
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello");
  call_expr->add_args()->mutable_const_expr()->set_string_value("he");
  call_expr->add_args()->mutable_const_expr()->set_string_value("we");

  // Should throw exception when string functions are disabled
  EXPECT_THROW(createExpression(*disabled_builder, expr), CelException);
}

TEST_F(StringFunctionsTest, DefaultConfigurationDisablesStringFunctions) {
  // Create builder with default configuration.
  auto default_builder = createBuilder({});

  // Create replace expression: "hello".replace("he", "we")
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello");
  call_expr->add_args()->mutable_const_expr()->set_string_value("he");
  call_expr->add_args()->mutable_const_expr()->set_string_value("we");

  // Should throw exception with default configuration (string functions disabled)
  EXPECT_THROW(createExpression(*default_builder, expr), CelException);
}

TEST_F(StringFunctionsTest, StringConversionAndConcatEnabled) {
  Protobuf::Arena arena;

  // Test string conversion: string(123)
  cel::expr::Expr conv_expr;
  conv_expr.mutable_call_expr()->set_function("string");
  conv_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_int64_value(123);

  auto cel_conv = createExpression(*builder_, conv_expr);
  auto conv_result = cel_conv->Evaluate(*activation_, &arena);

  ASSERT_TRUE(conv_result.ok());
  EXPECT_TRUE(conv_result.value().IsString());
  EXPECT_EQ(conv_result.value().StringOrDie().value(), "123");

  // Test string concatenation: "foo" + "bar"
  cel::expr::Expr concat_expr;
  concat_expr.mutable_call_expr()->set_function("_+_");
  concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("foo");
  concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("bar");

  auto cel_concat = createExpression(*builder_, concat_expr);
  auto concat_result = cel_concat->Evaluate(*activation_, &arena);

  ASSERT_TRUE(concat_result.ok());
  EXPECT_TRUE(concat_result.value().IsString());
  EXPECT_EQ(concat_result.value().StringOrDie().value(), "foobar");
}

// Test contains() function for substring checking.
TEST_F(StringFunctionsTest, ContainsFunction) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("contains");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello world");
  call_expr->add_args()->mutable_const_expr()->set_string_value("world");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsBool());
  EXPECT_TRUE(result.value().BoolOrDie());
}

// Test startsWith() function for prefix checking.
TEST_F(StringFunctionsTest, StartsWithFunction) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("startsWith");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello world");
  call_expr->add_args()->mutable_const_expr()->set_string_value("hello");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsBool());
  EXPECT_TRUE(result.value().BoolOrDie());
}

// Test endsWith() function for suffix checking.
TEST_F(StringFunctionsTest, EndsWithFunction) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("endsWith");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello world");
  call_expr->add_args()->mutable_const_expr()->set_string_value("world");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsBool());
  EXPECT_TRUE(result.value().BoolOrDie());
}

// Test contains() function with false result.
TEST_F(StringFunctionsTest, ContainsFunctionFalse) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("contains");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello world");
  call_expr->add_args()->mutable_const_expr()->set_string_value("xyz");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsBool());
  EXPECT_FALSE(result.value().BoolOrDie());
}

// Test startsWith() function with false result.
TEST_F(StringFunctionsTest, StartsWithFunctionFalse) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("startsWith");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello world");
  call_expr->add_args()->mutable_const_expr()->set_string_value("world");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsBool());
  EXPECT_FALSE(result.value().BoolOrDie());
}

// Test endsWith() function with false result.
TEST_F(StringFunctionsTest, EndsWithFunctionFalse) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("endsWith");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello world");
  call_expr->add_args()->mutable_const_expr()->set_string_value("hello");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsBool());
  EXPECT_FALSE(result.value().BoolOrDie());
}

// Test lowerAscii() function for converting the string to lower case.
TEST_F(StringFunctionsTest, LowerAsciiFunction) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("lowerAscii");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("HELLO");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "hello");
}

// Test upperAscii() function for converting the string to upper case.
TEST_F(StringFunctionsTest, UpperAsciiFunction) {
  cel::expr::Expr expr;
  auto* call_expr = expr.mutable_call_expr();
  call_expr->set_function("upperAscii");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello");

  auto cel_expr = createExpression(*builder_, expr);
  Protobuf::Arena arena;
  auto result = cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "HELLO");
}

// Test string functions with empty strings.
TEST_F(StringFunctionsTest, StringFunctionsWithEmptyStrings) {
  Protobuf::Arena arena;

  // Test contains with empty search string
  cel::expr::Expr contains_expr;
  auto* call_expr = contains_expr.mutable_call_expr();
  call_expr->set_function("contains");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello");
  call_expr->add_args()->mutable_const_expr()->set_string_value("");

  auto cel_expr = createExpression(*builder_, contains_expr);
  auto result = cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsBool());
  EXPECT_TRUE(result.value().BoolOrDie()); // Empty string is contained in any string

  // Test lowerAscii with empty string
  cel::expr::Expr lower_expr;
  auto* lower_call = lower_expr.mutable_call_expr();
  lower_call->set_function("lowerAscii");
  lower_call->mutable_target()->mutable_const_expr()->set_string_value("");

  auto lower_cel_expr = createExpression(*builder_, lower_expr);
  auto lower_result = lower_cel_expr->Evaluate(*activation_, &arena);

  ASSERT_TRUE(lower_result.ok());
  EXPECT_TRUE(lower_result.value().IsString());
  EXPECT_EQ(lower_result.value().StringOrDie().value(), "");
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
