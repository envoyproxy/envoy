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
  auto builder = createBuilder(&arena, makeOptRef(config));
  EXPECT_NE(builder, nullptr);
}

// Test getArenaOptimizedBuilder with null arena.
TEST_F(EvaluatorArenaTest, GetArenaOptimizedBuilderNullArena) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(true);

  // Should return nullptr when arena is null.
  auto builder = getArenaOptimizedBuilder(nullptr, config);
  EXPECT_EQ(builder, nullptr);
}

// Test getArenaOptimizedBuilder with constant folding disabled.
TEST_F(EvaluatorArenaTest, GetArenaOptimizedBuilderConstantFoldingDisabled) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(false);

  Protobuf::Arena arena;

  // Should return nullptr when constant folding is disabled.
  auto builder = getArenaOptimizedBuilder(&arena, config);
  EXPECT_EQ(builder, nullptr);
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

// Test CreateWithArena with null arena.
TEST_F(EvaluatorArenaTest, CreateWithArenaNullArena) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(true);

  cel::expr::Expr expr;
  expr.mutable_const_expr()->set_bool_value(true);

  // Should return error status when arena is null.
  auto result = CompiledExpression::CreateWithArena(nullptr, expr, config);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Arena-optimized builder requires both arena"));
}

// Test CreateWithArena with constant folding disabled.
TEST_F(EvaluatorArenaTest, CreateWithArenaConstantFoldingDisabled) {
  envoy::config::core::v3::CelExpressionConfig config;
  config.set_enable_constant_folding(false);

  cel::expr::Expr expr;
  expr.mutable_const_expr()->set_bool_value(true);

  Protobuf::Arena arena;

  // Should return error status when constant folding is disabled.
  auto result = CompiledExpression::CreateWithArena(&arena, expr, config);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Arena-optimized builder requires both arena"));
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
