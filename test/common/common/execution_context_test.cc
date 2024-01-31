#include "envoy/common/execution_context.h"

#include "gtest/gtest.h"

namespace Envoy {

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

} // namespace Envoy
