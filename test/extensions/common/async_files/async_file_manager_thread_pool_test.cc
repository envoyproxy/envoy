#include <memory>
#include <string>
#include <thread>

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/common/async_files/posix_file_operations.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/barrier.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

enum class BlockerState {
  Start,
  BlockingDuringExecution,
  UnblockedExecution,
  ExecutionFinished,
  BlockingDuringCallback,
  UnblockedCallback
};

class AsyncFileActionBlockedUntilReleased : public AsyncFileActionWithResult<bool> {
public:
  explicit AsyncFileActionBlockedUntilReleased(std::atomic<BlockerState>& state_out)
      : AsyncFileActionWithResult([this](bool result) { onComplete(result); }),
        state_out_(state_out) {
    absl::MutexLock lock(&blocking_mutex_);
    state_out_.store(BlockerState::Start);
  }
  void setState(BlockerState state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) {
    stage_ = state;
    state_out_.store(state);
  }
  bool executeImpl() final {
    absl::MutexLock lock(&blocking_mutex_);
    ASSERT(stage_ == BlockerState::Start);
    setState(BlockerState::BlockingDuringExecution);
    auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) {
      return stage_ == BlockerState::UnblockedExecution;
    };
    blocking_mutex_.Await(absl::Condition(&condition));
    setState(BlockerState::ExecutionFinished);
    return true;
  }
  void onComplete(bool result ABSL_ATTRIBUTE_UNUSED) {
    absl::MutexLock lock(&blocking_mutex_);
    setState(BlockerState::BlockingDuringCallback);
    auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) {
      return stage_ == BlockerState::UnblockedCallback;
    };
    blocking_mutex_.Await(absl::Condition(&condition));
  }
  bool waitUntilExecutionBlocked() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    absl::MutexLock lock(&blocking_mutex_);
    auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) {
      return stage_ == BlockerState::BlockingDuringExecution;
    };
    return blocking_mutex_.AwaitWithTimeout(absl::Condition(&condition), absl::Seconds(1));
  }
  bool waitUntilCallbackBlocked() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    absl::MutexLock lock(&blocking_mutex_);
    auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) {
      return stage_ == BlockerState::BlockingDuringCallback;
    };
    return blocking_mutex_.AwaitWithTimeout(absl::Condition(&condition), absl::Seconds(1));
  }
  bool unblockExecution() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    absl::MutexLock lock(&blocking_mutex_);
    if (stage_ != BlockerState::BlockingDuringExecution) {
      return false;
    }
    setState(BlockerState::UnblockedExecution);
    return true;
  }
  bool unblockCallback() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    absl::MutexLock lock(&blocking_mutex_);
    if (stage_ != BlockerState::BlockingDuringCallback) {
      return false;
    }
    setState(BlockerState::UnblockedCallback);
    return true;
  }
  bool isStarted() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    absl::MutexLock lock(&blocking_mutex_);
    auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(blocking_mutex_) {
      return stage_ != BlockerState::Start;
    };
    // Very short timeout because we can be expecting to fail this.
    return blocking_mutex_.AwaitWithTimeout(absl::Condition(&condition), absl::Milliseconds(30));
  }
  bool doWholeFlow() ABSL_LOCKS_EXCLUDED(blocking_mutex_) {
    return waitUntilExecutionBlocked() && unblockExecution() && waitUntilCallbackBlocked() &&
           unblockCallback();
  }

private:
  absl::Mutex blocking_mutex_;
  BlockerState stage_ GUARDED_BY(blocking_mutex_) = BlockerState::Start;
  std::atomic<BlockerState>& state_out_;
};

class AsyncFileManagerTest : public testing::Test {
protected:
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::string tmpdir_ = test_tmpdir ? test_tmpdir : "/tmp";
  absl::Mutex control_mutex_;
  std::unique_ptr<AsyncFileManager> manager_;

  AsyncFileActionBlockedUntilReleased* blocker_[3];
  std::atomic<BlockerState> blocker_last_state_[3];
  // returns the cancellation function.
  std::function<void()> enqueueBlocker(int index) {
    auto blocker =
        std::make_shared<AsyncFileActionBlockedUntilReleased>(blocker_last_state_[index]);
    blocker_[index] = blocker.get();
    return manager_->enqueue(std::move(blocker));
  }
};

using AsyncFileManagerDeathTest = AsyncFileManagerTest;

TEST_F(AsyncFileManagerDeathTest, PanicsWithUnspecifiedConfig) {
  AsyncFileManagerConfig config;
  EXPECT_DEATH({ manager_ = config.createManager(); }, "Invalid AsyncFileManagerConfig");
}

TEST_F(AsyncFileManagerTest, WorksWithThreadPoolSizeZero) {
  AsyncFileManagerConfig config;
  config.thread_pool_size = 0;
  manager_ = config.createManager();
  // A crafty regex to string-match any number higher than 0, including a
  // number that ends in 0.
  EXPECT_THAT(manager_->describe(), testing::ContainsRegex("thread_pool_size = [1-9]\\d*"));
  enqueueBlocker(0);
  EXPECT_TRUE(blocker_[0]->doWholeFlow());
  manager_.reset();
}

TEST_F(AsyncFileManagerTest, ThreadsBlockAppropriately) {
  AsyncFileManagerConfig config;
  config.thread_pool_size = 2;
  manager_ = config.createManager();
  EXPECT_THAT(manager_->describe(), testing::ContainsRegex("thread_pool_size = 2"));
  enqueueBlocker(0);
  enqueueBlocker(1);
  ASSERT_TRUE(blocker_[0]->waitUntilExecutionBlocked());
  ASSERT_TRUE(blocker_[1]->waitUntilExecutionBlocked());
  enqueueBlocker(2);
  // With 2 threads blocked, a third action should not start.
  EXPECT_FALSE(blocker_[2]->isStarted());
  ASSERT_TRUE(blocker_[0]->doWholeFlow());
  // When one of the workers finishes, the third action should be able to start.
  EXPECT_TRUE(blocker_[2]->isStarted());
  EXPECT_TRUE(blocker_[1]->doWholeFlow());
  EXPECT_TRUE(blocker_[2]->doWholeFlow());
  manager_.reset();
}

class AsyncFileManagerSingleThreadTest : public AsyncFileManagerTest {
public:
  void SetUp() override {
    AsyncFileManagerConfig config;
    config.thread_pool_size = 1;
    manager_ = config.createManager();
  }
};

TEST_F(AsyncFileManagerSingleThreadTest, AbortingDuringExecutionCancelsTheCallback) {
  auto cancelBlocker0 = enqueueBlocker(0);
  ASSERT_TRUE(blocker_[0]->waitUntilExecutionBlocked());
  enqueueBlocker(1);
  ASSERT_FALSE(blocker_[1]->isStarted());
  cancelBlocker0();
  blocker_[0]->unblockExecution();
  ASSERT_TRUE(blocker_[1]->doWholeFlow());
  EXPECT_EQ(BlockerState::ExecutionFinished, blocker_last_state_[0].load());
}

TEST_F(AsyncFileManagerSingleThreadTest, AbortingBeforeExecutionCancelsTheExecution) {
  enqueueBlocker(0);
  ASSERT_TRUE(blocker_[0]->waitUntilExecutionBlocked());
  auto cancelBlocker1 = enqueueBlocker(1);
  cancelBlocker1();
  EXPECT_TRUE(blocker_[0]->doWholeFlow());
  // Blocker 1 should never start, having been cancelled before it
  // was popped from the queue. We can't check its internal value because
  // it should also have been deleted, so we can only check its output state.
  EXPECT_EQ(BlockerState::Start, blocker_last_state_[1].load());
}

TEST_F(AsyncFileManagerSingleThreadTest, AbortingDuringCallbackBlocksUntilCallbackCompletes) {
  auto cancel = enqueueBlocker(0);
  blocker_[0]->waitUntilExecutionBlocked();
  blocker_[0]->unblockExecution();
  blocker_[0]->waitUntilCallbackBlocked();
  auto delayed_action_occurred = std::make_unique<std::atomic<bool>>(false);
  std::thread callback_unblocker([this, delayed_action_occurred = delayed_action_occurred.get()] {
    absl::Mutex mu;
    // Using AwaitWithTimeout because lint forbids us from sleeping in
    // real-time, but here we're forcing a race to go a specific way, using
    // real-time because there's no other practical option here.
    auto cond = []() { return false; };
    absl::MutexLock guard(&mu);
    mu.AwaitWithTimeout(absl::Condition(&cond), absl::Milliseconds(50));
    delayed_action_occurred->store(true);
    blocker_[0]->unblockCallback();
  });
  cancel();
  EXPECT_TRUE(delayed_action_occurred->load());
  EXPECT_EQ(BlockerState::UnblockedCallback, blocker_last_state_[0].load());
  callback_unblocker.join();
}

TEST_F(AsyncFileManagerSingleThreadTest, AbortingAfterCallbackHasNoObservableEffect) {
  auto cancel = enqueueBlocker(0);
  EXPECT_TRUE(blocker_[0]->doWholeFlow());
  cancel();
  EXPECT_EQ(BlockerState::UnblockedCallback, blocker_last_state_[0].load());
}

template <typename T> class WaitForResult {
public:
  std::function<void(T)> callback() {
    return [this](T result) { saveResult(result); };
  }
  void saveResult(T result) {
    result_ = result;
    barrier_.Block();
  }
  T getResult() {
    barrier_.Block();
    return result_;
  }

private:
  absl::Barrier barrier_{2};
  T result_;
};

TEST_F(AsyncFileManagerSingleThreadTest, CreateAnonymousFileWorks) {
  WaitForResult<absl::StatusOr<AsyncFileHandle>> handle_blocker;
  manager_->createAnonymousFile(tmpdir_, handle_blocker.callback());
  AsyncFileHandle handle = handle_blocker.getResult().value();
  // Open a second one, to ensure we get two distinct files
  // (and for coverage, because the second one doesn't use the once_flag path)
  WaitForResult<absl::StatusOr<AsyncFileHandle>> second_handle_blocker;
  manager_->createAnonymousFile(tmpdir_, second_handle_blocker.callback());
  AsyncFileHandle second_handle = second_handle_blocker.getResult().value();
  WaitForResult<absl::Status> close_blocker;
  handle->close(close_blocker.callback());
  absl::Status status = close_blocker.getResult();
  EXPECT_EQ(absl::OkStatus(), status);
  WaitForResult<absl::Status> second_close_blocker;
  second_handle->close(second_close_blocker.callback());
  status = second_close_blocker.getResult();
  EXPECT_EQ(absl::OkStatus(), status);
}

TEST_F(AsyncFileManagerSingleThreadTest, OpenExistingFileAndUnlinkWork) {
  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/async_file.XXXXXX", tmpdir_.c_str());
  int fd = realPosixFileOperations().mkstemp(filename);
  realPosixFileOperations().close(fd);
  WaitForResult<absl::StatusOr<AsyncFileHandle>> handle_blocker;
  manager_->openExistingFile(filename, AsyncFileManager::Mode::ReadWrite,
                             handle_blocker.callback());
  AsyncFileHandle handle = handle_blocker.getResult().value();
  WaitForResult<absl::Status> close_blocker;
  handle->close(close_blocker.callback());
  absl::Status status = close_blocker.getResult();
  EXPECT_EQ(absl::OkStatus(), status);
  WaitForResult<absl::Status> unlink_blocker;
  manager_->unlink(filename, unlink_blocker.callback());
  status = unlink_blocker.getResult();
  EXPECT_EQ(absl::OkStatus(), status);
  struct stat s;
  EXPECT_EQ(-1, stat(filename, &s));
}

TEST_F(AsyncFileManagerSingleThreadTest, OpenExistingFileFailsForNonexistent) {
  WaitForResult<absl::StatusOr<AsyncFileHandle>> handle_blocker;
  manager_->openExistingFile(absl::StrCat(tmpdir_, "/nonexistent_file"),
                             AsyncFileManager::Mode::ReadWrite, handle_blocker.callback());
  absl::Status status = handle_blocker.getResult().status();
  EXPECT_EQ(absl::StatusCode::kNotFound, status.code()) << status;
}

TEST_F(AsyncFileManagerSingleThreadTest, UnlinkFailsForNonexistent) {
  WaitForResult<absl::StatusOr<AsyncFileHandle>> handle_blocker;
  manager_->unlink(absl::StrCat(tmpdir_, "/nonexistent_file"), handle_blocker.callback());
  absl::Status status = handle_blocker.getResult().status();
  EXPECT_EQ(absl::StatusCode::kNotFound, status.code()) << status;
}

class AsyncFileManagerFactoryDummy : public AsyncFileManagerFactory {
  bool shouldUseThisFactory(const AsyncFileManagerConfig&) const override { return false; }
  std::unique_ptr<AsyncFileManager> create(const AsyncFileManagerConfig&) const override {
    return std::unique_ptr<AsyncFileManager>();
  }
};

TEST(DestructorCoverageTest, AsyncFileManagerFactoryDestructorDoesntMeltYourFaceOff) {
  AsyncFileManagerFactoryDummy dummy;
  // This test just provides coverage of the mandatory virtual destructor on
  // AsyncFileManagerFactory. The destructor doesn't do anything, so we don't actually have to test
  // anything, but being a registry factory class, the destructor doesn't get called any natural
  // way.
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
