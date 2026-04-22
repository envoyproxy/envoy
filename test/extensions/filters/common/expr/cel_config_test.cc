#include "source/extensions/filters/common/expr/evaluator.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "cel/expr/syntax.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace {

using ::testing::NiceMock;

class CelConfigTest : public testing::Test {
protected:
  void SetUp() override { stream_info_ = std::make_shared<NiceMock<StreamInfo::MockStreamInfo>>(); }

  std::shared_ptr<NiceMock<StreamInfo::MockStreamInfo>> stream_info_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(CelConfigTest, StringConversionEnabled) {
  Protobuf::Arena arena;

  // Create config with string conversion enabled.
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_string_conversion(true);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(false);

  // Get builder with configuration.
  auto builder = getBuilder(context_, config);
  ASSERT_NE(builder, nullptr);

  // Create string conversion expression: string(123)
  cel::expr::Expr string_conv_expr;
  string_conv_expr.mutable_call_expr()->set_function("string");
  string_conv_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_int64_value(123);

  // String conversion should work.
  auto compiled = CompiledExpression::Create(builder, string_conv_expr);
  ASSERT_TRUE(compiled.ok());

  auto activation = createActivation(nullptr, *stream_info_, nullptr, nullptr, nullptr);
  auto result = compiled.value().evaluate(*activation, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "123");
}

TEST_F(CelConfigTest, StringConcatEnabled) {
  Protobuf::Arena arena;

  // Create config with string concatenation enabled.
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(true);
  config.set_enable_string_functions(false);

  // Get builder with configuration.
  auto builder = getBuilder(context_, config);
  ASSERT_NE(builder, nullptr);

  // Create string concatenation expression: "foo" + "bar"
  cel::expr::Expr string_concat_expr;
  string_concat_expr.mutable_call_expr()->set_function("_+_");
  string_concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("foo");
  string_concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("bar");

  // String concatenation should work.
  auto compiled = CompiledExpression::Create(builder, string_concat_expr);
  ASSERT_TRUE(compiled.ok());

  auto activation = createActivation(nullptr, *stream_info_, nullptr, nullptr, nullptr);
  auto result = compiled.value().evaluate(*activation, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "foobar");
}

TEST_F(CelConfigTest, StringFunctionsEnabled) {
  Protobuf::Arena arena;

  // Create config with string functions enabled.
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(true);

  // Get builder with configuration.
  auto builder = getBuilder(context_, config);
  ASSERT_NE(builder, nullptr);

  // Create replace expression: "HELLO".replace("HE", "he")
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("HELLO");
  call_expr->add_args()->mutable_const_expr()->set_string_value("HE");
  call_expr->add_args()->mutable_const_expr()->set_string_value("he");

  // replace should work.
  auto compiled = CompiledExpression::Create(builder, replace_expr);
  ASSERT_TRUE(compiled.ok());

  auto activation = createActivation(nullptr, *stream_info_, nullptr, nullptr, nullptr);
  auto result = compiled.value().evaluate(*activation, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "heLLO");
}

TEST_F(CelConfigTest, StringFunctionsDisabled) {
  // Create config with string functions disabled (default).
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(false);

  // Get builder with configuration.
  auto builder = getBuilder(context_, config);
  ASSERT_NE(builder, nullptr);

  // Create replace expression: "HELLO".replace("H", "h")
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("HELLO");
  call_expr->add_args()->mutable_const_expr()->set_string_value("H");
  call_expr->add_args()->mutable_const_expr()->set_string_value("h");

  // replace should fail when string functions are disabled.
  auto compiled = CompiledExpression::Create(builder, replace_expr);
  EXPECT_FALSE(compiled.ok());
}

TEST_F(CelConfigTest, DefaultConfiguration) {
  // Get builder with default configuration.
  auto builder = getBuilder(context_);
  ASSERT_NE(builder, nullptr);

  // Create replace expression: "HELLO".replace("H", "h")
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("HELLO");
  call_expr->add_args()->mutable_const_expr()->set_string_value("H");
  call_expr->add_args()->mutable_const_expr()->set_string_value("h");

  // replace should fail with default configuration (string functions disabled).
  auto compiled = CompiledExpression::Create(builder, replace_expr);
  EXPECT_FALSE(compiled.ok());
}

// TODO(cel): This test is temporarily disabled because the MockServerFactoryContext
// doesn't properly handle singleton caching. In production, the BuilderCache singleton
// will correctly cache builders with the same configuration.
TEST_F(CelConfigTest, DISABLED_BuilderCaching) {
  // Create two configs with the same settings.
  envoy::config::core::v3::CelExpressionConfig config1;
  config1.set_enable_string_conversion(true);
  config1.set_enable_string_concat(true);
  config1.set_enable_string_functions(false);

  envoy::config::core::v3::CelExpressionConfig config2;
  config2.set_enable_string_conversion(true);
  config2.set_enable_string_concat(true);
  config2.set_enable_string_functions(false);

  // Get builders with the same configuration.
  auto builder1 = getBuilder(context_, config1);
  auto builder2 = getBuilder(context_, config2);

  // They should be the same cached instance.
  EXPECT_EQ(builder1.get(), builder2.get());

  // Create a different config.
  envoy::config::core::v3::CelExpressionConfig config3;
  config3.set_enable_string_conversion(false);
  config3.set_enable_string_concat(false);
  config3.set_enable_string_functions(true);

  // Get builder with different configuration.
  auto builder3 = getBuilder(context_, config3);

  // It should be a different instance.
  EXPECT_NE(builder1.get(), builder3.get());
}

TEST_F(CelConfigTest, CreateWithConfig) {
  Protobuf::Arena arena;

  // Create config with string functions enabled.
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(true);

  // Create replace expression: "hello".replace("he", "we")
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("hello");
  call_expr->add_args()->mutable_const_expr()->set_string_value("he");
  call_expr->add_args()->mutable_const_expr()->set_string_value("we");

  // Create expression with configuration.
  auto compiled = CompiledExpression::Create(context_, replace_expr, makeOptRef(config));
  ASSERT_TRUE(compiled.ok());

  auto activation = createActivation(nullptr, *stream_info_, nullptr, nullptr, nullptr);
  auto result = compiled.value().evaluate(*activation, &arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "wello");
}

// Test createBuilder with null arena.
TEST_F(CelConfigTest, CreateBuilderForArenaNullArena) {
  auto builder = createBuilder({}, nullptr);
  EXPECT_NE(builder, nullptr);
}

// Test createBuilder with valid arena and default config.
TEST_F(CelConfigTest, CreateBuilderForArenaWithArena) {
  Protobuf::Arena arena;
  auto builder = createBuilder({}, &arena);
  EXPECT_NE(builder, nullptr);
}

// Test createBuilder with arena and string functions enabled.
TEST_F(CelConfigTest, CreateBuilderForArenaStringFunctions) {
  Protobuf::Arena arena;
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_string_functions(true);

  auto builder_ptr = createBuilder(makeOptRef(config), &arena);
  auto builder = std::make_shared<BuilderInstance>(std::move(builder_ptr), nullptr);
  ASSERT_NE(builder, nullptr);

  // Create replace expression to verify string functions work.
  cel::expr::Expr replace_expr;
  auto* call_expr = replace_expr.mutable_call_expr();
  call_expr->set_function("replace");
  call_expr->mutable_target()->mutable_const_expr()->set_string_value("HELLO");
  call_expr->add_args()->mutable_const_expr()->set_string_value("HE");
  call_expr->add_args()->mutable_const_expr()->set_string_value("he");

  auto compiled = CompiledExpression::Create(builder, replace_expr);
  ASSERT_TRUE(compiled.ok());

  Protobuf::Arena eval_arena;
  auto activation = createActivation(nullptr, *stream_info_, nullptr, nullptr, nullptr);
  auto result = compiled.value().evaluate(*activation, &eval_arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "heLLO");
}

// Test createBuilder with arena and all features enabled.
TEST_F(CelConfigTest, CreateBuilderForArenaAllFeatures) {
  Protobuf::Arena arena;
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_string_conversion(true);
  config.set_enable_string_concat(true);
  config.set_enable_string_functions(true);

  auto builder_ptr = createBuilder(makeOptRef(config), &arena);
  auto builder = std::make_shared<BuilderInstance>(std::move(builder_ptr), nullptr);
  ASSERT_NE(builder, nullptr);

  // Test string concatenation: "foo" + "bar"
  cel::expr::Expr concat_expr;
  concat_expr.mutable_call_expr()->set_function("_+_");
  concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("foo");
  concat_expr.mutable_call_expr()->add_args()->mutable_const_expr()->set_string_value("bar");

  auto compiled = CompiledExpression::Create(builder, concat_expr);
  ASSERT_TRUE(compiled.ok());

  Protobuf::Arena eval_arena;
  auto activation = createActivation(nullptr, *stream_info_, nullptr, nullptr, nullptr);
  auto result = compiled.value().evaluate(*activation, &eval_arena);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().IsString());
  EXPECT_EQ(result.value().StringOrDie().value(), "foobar");
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
