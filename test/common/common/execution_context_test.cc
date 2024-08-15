#include "envoy/common/execution_context.h"
#include "envoy/http/filter_factory.h"

#include "source/common/tracing/null_span_impl.h"

#include "gtest/gtest.h"

namespace Envoy {

thread_local const Http::FilterContext* current_filter_context = nullptr;

class TestExecutionContext : public ExecutionContext {
public:
  int activationDepth() const { return activation_depth_; }
  int activationGenerations() const { return activation_generations_; }

private:
  void activate() override {
    if (activation_depth_ == 0) {
      activation_generations_++;
    }
    activation_depth_++;
  }

  void deactivate() override {
    EXPECT_GE(activation_depth_, 0);
    activation_depth_--;
  }

  ExecutionScope scopeForSpan(Envoy::Tracing::Span&) override { return ExecutionScope(); }

  ExecutionScope scopeForFilter(const Http::FilterContext& filter_context) override {
    const Http::FilterContext* old_filter_context = current_filter_context;
    current_filter_context = &filter_context;
    return ExecutionScope([old_filter_context]() { current_filter_context = old_filter_context; });
  }

  int activation_depth_ = 0;
  // How many times |activation_depth_| changed from 0 to 1.
  int activation_generations_ = 0;
};

TEST(ExecutionContextTest, NullContext) {
  ScopedExecutionContext scoped_context(nullptr);
  EXPECT_TRUE(scoped_context.isNull());

  ScopedExecutionContext scoped_context2;
  EXPECT_TRUE(scoped_context2.isNull());
}

TEST(ExecutionContextTest, NestedScopes) {
  TestExecutionContext context;
  EXPECT_EQ(context.activationDepth(), 0);
  EXPECT_EQ(context.activationGenerations(), 0);

  {
    ScopedExecutionContext scoped_context(&context);
    EXPECT_EQ(context.activationDepth(), 1);
    EXPECT_EQ(context.activationGenerations(), 1);
    {
      ScopedExecutionContext nested_scoped_context(&context);
      EXPECT_EQ(context.activationDepth(), 2);
      EXPECT_EQ(context.activationGenerations(), 1);
    }
    EXPECT_EQ(context.activationDepth(), 1);
    EXPECT_EQ(context.activationGenerations(), 1);
  }
  EXPECT_EQ(context.activationDepth(), 0);
  EXPECT_EQ(context.activationGenerations(), 1);
}

TEST(ExecutionContextTest, DisjointScopes) {
  TestExecutionContext context;

  for (int i = 1; i < 5; i++) {
    ScopedExecutionContext scoped_context(&context);
    EXPECT_EQ(context.activationDepth(), 1);
    EXPECT_EQ(context.activationGenerations(), i);
  }

  EXPECT_EQ(context.activationDepth(), 0);
}

TEST(ExecutionContextTest, NoopScope) {
  Http::FilterContext filter_context;
  ExecutionScope scope1 = ExecutionContext::makeScopeForFilter(nullptr, filter_context);
  EXPECT_FALSE(scope1.hasExitCallback());

  ExecutionScope scope2 = ExecutionContext::makeScopeForSpan(nullptr, nullptr);
  EXPECT_FALSE(scope2.hasExitCallback());

  TestExecutionContext context;
  ExecutionScope scope3 =
      ExecutionContext::makeScopeForSpan(&context, &Tracing::NullSpan::instance());
  EXPECT_FALSE(scope3.hasExitCallback());
}

TEST(ExecutionContextTest, FilterScope) {
  TestExecutionContext context;

  Http::FilterContext outer_filter_context{"outer_filter", "outer_filter"};
  ExecutionScope outer_scope = ExecutionContext::makeScopeForFilter(&context, outer_filter_context);
  EXPECT_TRUE(outer_scope.hasExitCallback());
  EXPECT_EQ(current_filter_context, &outer_filter_context);

  {
    Http::FilterContext inner_filter_context{"inner_filter", "inner_filter"};
    ExecutionScope inner_scope =
        ExecutionContext::makeScopeForFilter(&context, inner_filter_context);
    EXPECT_TRUE(inner_scope.hasExitCallback());
    EXPECT_EQ(current_filter_context, &inner_filter_context);
  }

  EXPECT_EQ(current_filter_context, &outer_filter_context);
}

} // namespace Envoy
