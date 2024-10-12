#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "envoy/extensions/common/async_files/v3/async_file_manager.pb.h"

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/common/async_files/async_file_manager_factory.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

using StatusHelpers::HasStatusCode;

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
  using AsyncFileActionWithResult::AsyncFileActionWithResult;
  bool executeImpl() final {
    executing_.set_value();
    bool ret = continue_executing_.get_future().wait_for(std::chrono::seconds(1)) ==
               std::future_status::ready;
    return ret;
  }
  bool waitUntilExecutionBlocked() {
    return executing_future_.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
  }
  bool isStarted() {
    return executing_future_.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready;
  }
  bool unblockExecution() {
    continue_executing_.set_value();
    return true;
  }
  bool doWholeFlow() { return waitUntilExecutionBlocked() && unblockExecution(); }

private:
  std::promise<void> executing_;
  std::future<void> executing_future_ = executing_.get_future();
  std::promise<void> continue_executing_;
};

class AsyncFileManagerTest : public testing::Test {
public:
  void SetUp() override {
    singleton_manager_ = std::make_unique<Singleton::ManagerImpl>();
    factory_ = AsyncFileManagerFactory::singleton(singleton_manager_.get());
  }

  void resolveFileActions() {
    manager_->waitForIdle();
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

protected:
  std::unique_ptr<Singleton::ManagerImpl> singleton_manager_;
  std::shared_ptr<AsyncFileManagerFactory> factory_;
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::string tmpdir_ = test_tmpdir ? test_tmpdir : "/tmp";
  absl::Mutex control_mutex_;
  std::shared_ptr<AsyncFileManager> manager_;

  AsyncFileActionBlockedUntilReleased* blocker_[3];
  std::vector<bool> blocker_callback_result_ = std::vector<bool>(3);
  // returns the cancellation function.
  CancelFunction enqueueBlocker(int index) {
    auto blocker = std::make_unique<AsyncFileActionBlockedUntilReleased>(
        [this, index](bool result) { blocker_callback_result_[index] = result; });
    blocker_[index] = blocker.get();
    return manager_->enqueue(dispatcher_.get(), std::move(blocker));
  }
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
};

TEST_F(AsyncFileManagerTest, WorksWithThreadPoolSizeZero) {
  envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
  config.mutable_thread_pool()->set_thread_count(0);
  manager_ = factory_->getAsyncFileManager(config);
  // A crafty regex to string-match any number higher than 0, including a
  // number that ends in 0.
  EXPECT_THAT(manager_->describe(), testing::ContainsRegex("thread_pool_size = [1-9]\\d*"));
  enqueueBlocker(0);
  EXPECT_TRUE(blocker_[0]->doWholeFlow());
  resolveFileActions();
  factory_.reset();
}

TEST_F(AsyncFileManagerTest, ThreadsBlockAppropriately) {
  envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
  config.mutable_thread_pool()->set_thread_count(2);
  manager_ = factory_->getAsyncFileManager(config);
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
  EXPECT_TRUE(blocker_[2]->doWholeFlow());
  EXPECT_TRUE(blocker_[1]->doWholeFlow());
  resolveFileActions();
  factory_.reset();
}

class AsyncFileManagerSingleThreadTest : public AsyncFileManagerTest {
public:
  void SetUp() override {
    envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
    config.mutable_thread_pool()->set_thread_count(1);
    singleton_manager_ = std::make_unique<Singleton::ManagerImpl>();
    auto factory = AsyncFileManagerFactory::singleton(singleton_manager_.get());
    manager_ = factory->getAsyncFileManager(config);
  }

protected:
  std::unique_ptr<Singleton::ManagerImpl> singleton_manager_;
};

TEST_F(AsyncFileManagerSingleThreadTest, CancellingDuringExecutionCancelsTheCallback) {
  EXPECT_FALSE(blocker_callback_result_[0]);
  CancelFunction cancelBlocker0 = enqueueBlocker(0);
  EXPECT_FALSE(blocker_callback_result_[0]);
  ASSERT_TRUE(blocker_[0]->waitUntilExecutionBlocked());
  enqueueBlocker(1);
  ASSERT_FALSE(blocker_[1]->isStarted());
  cancelBlocker0();
  blocker_[0]->unblockExecution();
  ASSERT_TRUE(blocker_[1]->doWholeFlow());
  resolveFileActions();
  EXPECT_FALSE(blocker_callback_result_[0]);
  EXPECT_TRUE(blocker_callback_result_[1]);
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
  resolveFileActions();
  EXPECT_TRUE(blocker_callback_result_[0]);
  EXPECT_FALSE(blocker_callback_result_[1]);
}

TEST_F(AsyncFileManagerSingleThreadTest, AbortingAfterCallbackHasNoObservableEffect) {
  auto cancel = enqueueBlocker(0);
  EXPECT_TRUE(blocker_[0]->doWholeFlow());
  resolveFileActions();
  cancel();
}

TEST_F(AsyncFileManagerSingleThreadTest, CreateAnonymousFileWorks) {
  AsyncFileHandle handle;
  manager_->createAnonymousFile(dispatcher_.get(), tmpdir_, [&](absl::StatusOr<AsyncFileHandle> h) {
    handle = std::move(h.value());
  });
  resolveFileActions();
  // Open a second one, to ensure we get two distinct files
  // (and for coverage, because the second one doesn't use the once_flag path)
  AsyncFileHandle second_handle;
  manager_->createAnonymousFile(dispatcher_.get(), tmpdir_, [&](absl::StatusOr<AsyncFileHandle> h) {
    second_handle = std::move(h.value());
  });
  resolveFileActions();
  EXPECT_THAT(handle, testing::NotNull());
  EXPECT_THAT(second_handle, testing::NotNull());
  EXPECT_NE(handle, second_handle);
  absl::Status close_result = absl::InternalError("not set");
  EXPECT_OK(handle->close(dispatcher_.get(), [&](absl::Status s) { close_result = std::move(s); }));
  absl::Status second_close_result = absl::InternalError("not set");
  EXPECT_OK(second_handle->close(dispatcher_.get(),
                                 [&](absl::Status s) { second_close_result = std::move(s); }));
  resolveFileActions();
  EXPECT_OK(close_result);
  EXPECT_OK(second_close_result);
}

TEST_F(AsyncFileManagerSingleThreadTest, OpenExistingFileStatAndUnlinkWork) {
  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/async_file.XXXXXX", tmpdir_.c_str());
  Api::OsSysCalls& posix = Api::OsSysCallsSingleton().get();
  auto fd = posix.mkstemp(filename);
  posix.close(fd.return_value_);
  AsyncFileHandle handle;
  manager_->openExistingFile(
      dispatcher_.get(), filename, AsyncFileManager::Mode::ReadWrite,
      [&](absl::StatusOr<AsyncFileHandle> h) { handle = std::move(h.value()); });
  resolveFileActions();
  absl::Status close_result;
  EXPECT_OK(handle->close(dispatcher_.get(), [&](absl::Status s) { close_result = std::move(s); }));
  EXPECT_OK(close_result);
  absl::StatusOr<struct stat> stat_result;
  manager_->stat(dispatcher_.get(), filename,
                 [&](absl::StatusOr<struct stat> result) { stat_result = std::move(result); });
  resolveFileActions();
  EXPECT_OK(stat_result);
  EXPECT_EQ(0, stat_result.value().st_size);
  absl::Status unlink_result = absl::InternalError("not set");
  manager_->unlink(dispatcher_.get(), filename,
                   [&](absl::Status s) { unlink_result = std::move(s); });
  resolveFileActions();
  EXPECT_OK(unlink_result);
  // Make sure unlink deleted the file.
  struct stat s;
  EXPECT_EQ(-1, stat(filename, &s));
}

TEST_F(AsyncFileManagerSingleThreadTest, OpenExistingFileFailsForNonexistent) {
  absl::StatusOr<AsyncFileHandle> handle_result;
  manager_->openExistingFile(dispatcher_.get(), absl::StrCat(tmpdir_, "/nonexistent_file"),
                             AsyncFileManager::Mode::ReadWrite,
                             [&](absl::StatusOr<AsyncFileHandle> r) { handle_result = r; });
  resolveFileActions();
  EXPECT_THAT(handle_result, HasStatusCode(absl::StatusCode::kNotFound));
}

TEST_F(AsyncFileManagerSingleThreadTest, StatFailsForNonexistent) {
  absl::StatusOr<struct stat> stat_result;
  manager_->stat(dispatcher_.get(), absl::StrCat(tmpdir_, "/nonexistent_file"),
                 [&](absl::StatusOr<struct stat> r) { stat_result = std::move(r); });
  resolveFileActions();
  EXPECT_THAT(stat_result, HasStatusCode(absl::StatusCode::kNotFound));
}

TEST_F(AsyncFileManagerSingleThreadTest, UnlinkFailsForNonexistent) {
  absl::Status unlink_result;
  manager_->unlink(dispatcher_.get(), absl::StrCat(tmpdir_, "/nonexistent_file"),
                   [&](absl::Status s) { unlink_result = std::move(s); });
  resolveFileActions();
  EXPECT_THAT(unlink_result, HasStatusCode(absl::StatusCode::kNotFound));
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
