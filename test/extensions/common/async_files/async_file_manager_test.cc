#include <memory>
#include <string>
#include <thread>

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileManagerTest : public testing::Test {
protected:
  enum class ContextCallbackState { NotRunning = 0, Blocking = 1, Unblocked = 2 };

  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::string tmpdir_ = test_tmpdir ? test_tmpdir : "/tmp";
  absl::Mutex control_mutex_;
  void stateSet(ContextCallbackState* state, ContextCallbackState new_value) {
    absl::MutexLock lock(&control_mutex_);
    *state = new_value;
  }
  void stateWait(ContextCallbackState* state, ContextCallbackState desired_value) {
    absl::MutexLock lock(&control_mutex_);
    auto condition = [state, desired_value]() { return *state == desired_value; };
    if (!control_mutex_.AwaitWithTimeout(absl::Condition(&condition), absl::Seconds(1))) {
      FAIL() << "stateWait timed out";
    }
  }
  bool stateBecomes(ContextCallbackState* state, ContextCallbackState desired_value) {
    absl::MutexLock lock(&control_mutex_);
    auto condition = [state, desired_value]() { return *state == desired_value; };
    return control_mutex_.AwaitWithTimeout(absl::Condition(&condition), absl::Milliseconds(100));
  }
  std::function<void(absl::Status)> controlledBlockingCallback(ContextCallbackState* state) {
    return [this, state](absl::Status) {
      stateSet(state, ContextCallbackState::Blocking);
      stateWait(state, ContextCallbackState::Unblocked);
      stateSet(state, ContextCallbackState::NotRunning);
    };
  }
  std::function<void(absl::Status)> controlledBlockingCallbackThen(ContextCallbackState* state,
                                                                   std::function<void()> next) {
    return [this, state, next](absl::Status) {
      stateSet(state, ContextCallbackState::Blocking);
      stateWait(state, ContextCallbackState::Unblocked);
      next();
    };
  }
  std::unique_ptr<AsyncFileManager> manager_;
  ContextCallbackState ctx1state_ = ContextCallbackState::NotRunning;
  ContextCallbackState ctx2state_ = ContextCallbackState::NotRunning;
  ContextCallbackState ctx3state_ = ContextCallbackState::NotRunning;
  AsyncFileHandle ctx1_, ctx2_, ctx3_;
};

TEST_F(AsyncFileManagerTest, WorksWithDefaultConfig) {
  AsyncFileManagerConfig config;
  manager_ = config.createManager();
  EXPECT_GT(manager_->threadPoolSize(), 0U);
  ctx1_ = manager_->newFileHandle();
  ctx1_->whenReady(controlledBlockingCallback(&ctx1state_));
  stateWait(&ctx1state_, ContextCallbackState::Blocking);
  stateSet(&ctx1state_, ContextCallbackState::Unblocked);
  EXPECT_TRUE(stateBecomes(&ctx1state_, ContextCallbackState::NotRunning))
      << static_cast<int>(ctx1state_);
}

TEST_F(AsyncFileManagerTest, ThreadsBlockAppropriately) {
  AsyncFileManagerConfig config;
  config.thread_pool_size = 2;
  manager_ = config.createManager();
  EXPECT_EQ(2U, manager_->threadPoolSize());
  ctx1_ = manager_->newFileHandle();
  ctx2_ = manager_->newFileHandle();
  ctx3_ = manager_->newFileHandle();
  // Give ctx1 a two-step sequence of actions.
  ctx1_->whenReady(controlledBlockingCallbackThen(
      &ctx1state_, [&]() { ctx1_->whenReady(controlledBlockingCallback(&ctx1state_)); }));
  // Give ctx2 a single action.
  ctx2_->whenReady(controlledBlockingCallback(&ctx2state_));
  // Both states should hit their callback.
  EXPECT_TRUE(stateBecomes(&ctx1state_, ContextCallbackState::Blocking));
  EXPECT_TRUE(stateBecomes(&ctx2state_, ContextCallbackState::Blocking));
  // Give ctx3 a single action.
  ctx3_->whenReady(controlledBlockingCallback(&ctx3state_));
  // With a thread pool of size 2 and two contexts long-blocked on their callback
  // (which you should never do except for testing purposes!)
  // the third context should not call its callback as it never gets a thread.
  EXPECT_FALSE(stateBecomes(&ctx3state_, ContextCallbackState::Blocking));
  // Allow context 1 to advance to its second action.
  stateSet(&ctx1state_, ContextCallbackState::Unblocked);
  // It should block again in the second action.
  ASSERT_TRUE(stateBecomes(&ctx1state_, ContextCallbackState::Blocking));
  // And context 3 still didn't get a turn because context 1 chained its actions.
  EXPECT_FALSE(stateBecomes(&ctx3state_, ContextCallbackState::Blocking));
  // When context 1 is allowed to complete, context 3 should be able to enter its callback.
  stateSet(&ctx1state_, ContextCallbackState::Unblocked);
  EXPECT_TRUE(stateBecomes(&ctx1state_, ContextCallbackState::NotRunning));
  EXPECT_TRUE(stateBecomes(&ctx3state_, ContextCallbackState::Blocking));
  // Also allow the other two callbacks to complete.
  stateSet(&ctx2state_, ContextCallbackState::Unblocked);
  stateSet(&ctx3state_, ContextCallbackState::Unblocked);
  manager_.reset();
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
