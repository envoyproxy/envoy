#include "envoy/common/execution_context.h"

#include "gtest/gtest.h"

namespace Envoy {

class TestExecutionContext : public ExecutionContext {
public:
  int activation_depth() const { return activation_depth_; }
  int activation_generations() const { return activation_generations_; }

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

  int activation_depth_ = 0;
  // How many times |activation_depth_| changed from 0 to 1.
  int activation_generations_ = 0;
};

TEST(ExecutionContextTest, NullContext) {
  ScopedExecutionContext scoped_context(nullptr);
  EXPECT_TRUE(scoped_context.is_null());

  ScopedExecutionContext scoped_context2;
  EXPECT_TRUE(scoped_context2.is_null());
}

TEST(ExecutionContextTest, NestedScopes) {
  TestExecutionContext context;
  EXPECT_EQ(context.activation_depth(), 0);
  EXPECT_EQ(context.activation_generations(), 0);

  {
    ScopedExecutionContext scoped_context(&context);
    EXPECT_EQ(context.activation_depth(), 1);
    EXPECT_EQ(context.activation_generations(), 1);
    {
      ScopedExecutionContext nested_scoped_context(&context);
      EXPECT_EQ(context.activation_depth(), 2);
      EXPECT_EQ(context.activation_generations(), 1);
    }
    EXPECT_EQ(context.activation_depth(), 1);
    EXPECT_EQ(context.activation_generations(), 1);
  }
  EXPECT_EQ(context.activation_depth(), 0);
  EXPECT_EQ(context.activation_generations(), 1);
}

TEST(ExecutionContextTest, DisjointScopes) {
  TestExecutionContext context;

  for (int i = 1; i < 5; i++) {
    ScopedExecutionContext scoped_context(&context);
    EXPECT_EQ(context.activation_depth(), 1);
    EXPECT_EQ(context.activation_generations(), i);
  }

  EXPECT_EQ(context.activation_depth(), 0);
}

TEST(ExecutionContextTest, ExtendScopeByMove) {
  TestExecutionContext context;

  ScopedExecutionContext scoped_context1([&] {
    ScopedExecutionContext scoped_context0(&context);
    EXPECT_EQ(context.activation_depth(), 1);
    EXPECT_EQ(context.activation_generations(), 1);
    return scoped_context0;
  }());
  EXPECT_EQ(context.activation_depth(), 1);
  EXPECT_EQ(context.activation_generations(), 1);
  EXPECT_FALSE(scoped_context1.is_null());

  ScopedExecutionContext scoped_context2(std::move(scoped_context1));
  EXPECT_EQ(context.activation_depth(), 1);
  EXPECT_EQ(context.activation_generations(), 1);
  EXPECT_FALSE(scoped_context2.is_null());
  EXPECT_TRUE(scoped_context1.is_null());
}

} // namespace Envoy
