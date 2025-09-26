#include "source/extensions/filters/common/expr/evaluator.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace {

using ::testing::NiceMock;

// Test class for arena optimization functionality.
class EvaluatorArenaTest : public testing::Test {
protected:
  void SetUp() override {}

  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

// Test arena optimization in createBuilder function.
TEST_F(EvaluatorArenaTest, CreateBuilderWithArenaAndConstantFolding) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(true);
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(false);
  config.set_enable_string_functions(false);

  // Create arena to enable constant folding path.
  Protobuf::Arena arena;

  // Test constant folding enabled with arena provided.
  auto builder = createBuilder(&arena, &config);
  EXPECT_NE(builder, nullptr);
}

// Test arena optimization disabled when enable_constant_folding is false.
TEST_F(EvaluatorArenaTest, CreateBuilderWithArenaButNoConstantFolding) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(false);

  Protobuf::Arena arena;

  // Test constant folding disabled even with arena provided.
  auto builder = createBuilder(&arena, &config);
  EXPECT_NE(builder, nullptr);
}

// Test getBuilderWithArenaOptimization with constant folding enabled.
TEST_F(EvaluatorArenaTest, GetBuilderWithArenaOptimizationEnabled) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(true);

  Protobuf::Arena arena;

  // Should use arena-optimized builder when constant folding is enabled.
  auto builder = getBuilderWithArenaOptimization(context_.serverFactoryContext(), config, &arena);
  EXPECT_NE(builder, nullptr);
}

// Test getBuilderWithArenaOptimization fallback to cached builder.
TEST_F(EvaluatorArenaTest, GetBuilderWithArenaOptimizationDisabled) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(false);

  Protobuf::Arena arena;

  // Should fall back to cached builder when constant folding is disabled.
  auto builder = getBuilderWithArenaOptimization(context_.serverFactoryContext(), config, &arena);
  EXPECT_NE(builder, nullptr);
}

// Test getArenaOptimizedBuilder with valid configuration.
TEST_F(EvaluatorArenaTest, GetArenaOptimizedBuilderSuccess) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(true);

  Protobuf::Arena arena;

  // Should create builder with arena optimization enabled.
  auto builder = getArenaOptimizedBuilder(&arena, config);
  EXPECT_NE(builder, nullptr);
}

// Test getArenaOptimizedBuilder with null arena.
TEST_F(EvaluatorArenaTest, GetArenaOptimizedBuilderNullArena) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(true);

  // Should throw exception when arena is null.
  EXPECT_THROW_WITH_MESSAGE(
      getArenaOptimizedBuilder(nullptr, config), CelException,
      "Arena-optimized builder requires both arena and enable_constant_folding");
}

// Test getArenaOptimizedBuilder with constant folding disabled.
TEST_F(EvaluatorArenaTest, GetArenaOptimizedBuilderConstantFoldingDisabled) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(false);

  Protobuf::Arena arena;

  // Should throw exception when constant folding is disabled.
  EXPECT_THROW_WITH_MESSAGE(
      getArenaOptimizedBuilder(&arena, config), CelException,
      "Arena-optimized builder requires both arena and enable_constant_folding");
}

// Test CreateWithArena with valid arena and configuration.
TEST_F(EvaluatorArenaTest, CreateWithArenaSuccess) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(true);

  // Create a simple CEL expression.
  cel::expr::Expr expr;
  expr.mutable_const_expr()->set_bool_value(true);

  Protobuf::Arena arena;

  // Should successfully create expression with arena optimization.
  auto result = CompiledExpression::CreateWithArena(&arena, expr, config);
  ASSERT_TRUE(result.ok());
}

// Test CompiledExpression default configuration path.
TEST_F(EvaluatorArenaTest, CompiledExpressionCreateWithoutConfig) {
  cel::expr::Expr expr;
  expr.mutable_const_expr()->set_bool_value(true);

  // Test creation without explicit configuration.
  auto result = CompiledExpression::Create(context_.serverFactoryContext(), expr, nullptr);
  ASSERT_TRUE(result.ok());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Protobuf::Arena arena;

  // Test evaluation functionality.
  auto eval_result = result.value().evaluate(arena, &context_.serverFactoryContext().localInfo(),
                                             stream_info, nullptr, nullptr, nullptr);
  EXPECT_TRUE(eval_result.has_value());
}

// Test matches function with non-boolean result.
TEST_F(EvaluatorArenaTest, MatchesWithNonBooleanResult) {
  // Create expression that returns non-boolean (string).
  cel::expr::Expr expr;
  expr.mutable_const_expr()->set_string_value("not a boolean");

  auto builder = Extensions::Filters::Common::Expr::getBuilder(context_.serverFactoryContext());
  auto compiled_result = CompiledExpression::Create(builder, expr);
  ASSERT_TRUE(compiled_result.ok());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;

  // Non-boolean results should be treated as false.
  bool result = compiled_result.value().matches(stream_info, request_headers);
  EXPECT_FALSE(result);
}

// Test CreateWithArena with null arena.
TEST_F(EvaluatorArenaTest, CreateWithArenaNullArena) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(true);

  cel::expr::Expr expr;
  expr.mutable_const_expr()->set_bool_value(true);

  // Should throw exception when arena is null.
  EXPECT_THROW_WITH_MESSAGE(
      {
        [[maybe_unused]] auto result = CompiledExpression::CreateWithArena(nullptr, expr, config);
      },
      CelException, "Arena-optimized builder requires both arena and enable_constant_folding");
}

// Test CreateWithArena with constant folding disabled.
TEST_F(EvaluatorArenaTest, CreateWithArenaConstantFoldingDisabled) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(false);

  cel::expr::Expr expr;
  expr.mutable_const_expr()->set_bool_value(true);

  Protobuf::Arena arena;

  // Should throw exception when constant folding is disabled.
  EXPECT_THROW_WITH_MESSAGE(
      { [[maybe_unused]] auto result = CompiledExpression::CreateWithArena(&arena, expr, config); },
      CelException, "Arena-optimized builder requires both arena and enable_constant_folding");
}

// Test string function registration with enabled configuration.
TEST_F(EvaluatorArenaTest, CreateBuilderWithStringFunctionsEnabled) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_string_functions(true);
  config.set_enable_string_conversion(false);
  config.set_enable_string_concat(false);
  config.set_enable_constant_folding(false);

  // Test builder creation with string functions enabled.
  auto builder = createBuilder(nullptr, &config);
  EXPECT_NE(builder, nullptr);
}

// Test arena optimization error message generation.
TEST_F(EvaluatorArenaTest, ArenaOptimizationErrorMessageCoverage) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(false);

  try {
    // Test error message generation for invalid arena optimization.
    getArenaOptimizedBuilder(nullptr, config);
    FAIL() << "Expected CelException";
  } catch (const CelException& e) {
    EXPECT_THAT(e.what(), testing::HasSubstr("Arena-optimized builder requires both arena"));
  }
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
