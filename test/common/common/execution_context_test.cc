#include <memory>

#include "envoy/common/execution_context.h"

#include "source/common/api/api_impl.h"
#include "source/common/common/scope_tracker.h"

#include "test/mocks/common.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
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

class ExecutionContextTest : public testing::Test {
public:
  ExecutionContextTest() {
    ON_CALL(tracked_object_, trackedStream())
        .WillByDefault(testing::Return(OptRef<const StreamInfo::StreamInfo>(stream_info_)));
  }

  void setWithoutContext() {
    context_ = nullptr;
    stream_info_.filter_state_ = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::Connection);
  }
  void setWithContext() {
    context_ = std::make_shared<TestExecutionContext>();
    stream_info_.filter_state_ = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::Connection);
    stream_info_.filter_state_->setData(kConnectionExecutionContextFilterStateName, context_,
                                        StreamInfo::FilterState::StateType::ReadOnly,
                                        StreamInfo::FilterState::LifeSpan::Connection);
  }

  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  testing::NiceMock<MockScopeTrackedObject> tracked_object_;
  std::shared_ptr<TestExecutionContext> context_{};
};

TEST_F(ExecutionContextTest, NullContext) {
  {
    ScopedExecutionContext scoped_context(nullptr);
    EXPECT_TRUE(scoped_context.isNull());
  }
  {
    ScopedExecutionContext scoped_context;
    EXPECT_TRUE(scoped_context.isNull());
  }
  {
    setWithoutContext();
    ScopedExecutionContext scoped_context(&tracked_object_);
    EXPECT_TRUE(scoped_context.isNull());
  }
}

TEST_F(ExecutionContextTest, NestedScopes) {
  setWithContext();

  EXPECT_EQ(context_->activationDepth(), 0);
  EXPECT_EQ(context_->activationGenerations(), 0);

  {
    ScopedExecutionContext scoped_context(&tracked_object_);
    EXPECT_EQ(context_->activationDepth(), 1);
    EXPECT_EQ(context_->activationGenerations(), 1);
    {
      ScopedExecutionContext nested_scoped_context(&tracked_object_);
      EXPECT_EQ(context_->activationDepth(), 2);
      EXPECT_EQ(context_->activationGenerations(), 1);
    }
    EXPECT_EQ(context_->activationDepth(), 1);
    EXPECT_EQ(context_->activationGenerations(), 1);
  }
  EXPECT_EQ(context_->activationDepth(), 0);
  EXPECT_EQ(context_->activationGenerations(), 1);
}

TEST_F(ExecutionContextTest, DisjointScopes) {
  setWithContext();

  for (int i = 1; i < 5; i++) {
    ScopedExecutionContext scoped_context(&tracked_object_);
    EXPECT_EQ(context_->activationDepth(), 1);
    EXPECT_EQ(context_->activationGenerations(), i);
  }

  EXPECT_EQ(context_->activationDepth(), 0);
}

TEST_F(ExecutionContextTest, InScopeTrackerScopeState) {

  Api::ApiPtr api(Api::createApiForTest());
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  EXPECT_CALL(tracked_object_, trackedStream())
      .Times(2)
      .WillRepeatedly(testing::Return(OptRef<const StreamInfo::StreamInfo>(stream_info_)));

  setWithContext();
  EXPECT_EQ(context_->activationDepth(), 0);
  EXPECT_EQ(context_->activationGenerations(), 0);
  {
    ScopeTrackerScopeState scope(&tracked_object_, *dispatcher);
    EXPECT_EQ(context_->activationDepth(), 1);
    EXPECT_EQ(context_->activationGenerations(), 1);
  }

  EXPECT_EQ(context_->activationDepth(), 0);
  EXPECT_EQ(context_->activationGenerations(), 1);

  setWithoutContext();
  { ScopeTrackerScopeState scope(&tracked_object_, *dispatcher); }
}

} // namespace Envoy
