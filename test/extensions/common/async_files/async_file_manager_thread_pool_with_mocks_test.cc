#include <cerrno>
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

#include "test/mocks/api/mocks.h"
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

using StatusHelpers::IsOkAndHolds;
using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;

class AsyncFileManagerWithMockFilesTest : public ::testing::Test {
public:
  void SetUp() override {
    EXPECT_CALL(mock_posix_file_operations_, supportsAllPosixFileOperations())
        .WillRepeatedly(Return(true));
    envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
    config.mutable_thread_pool()->set_thread_count(1);
    singleton_manager_ = std::make_unique<Singleton::ManagerImpl>();
    factory_ = AsyncFileManagerFactory::singleton(singleton_manager_.get());
    manager_ = factory_->getAsyncFileManager(config, &mock_posix_file_operations_);
  }

protected:
  std::unique_ptr<Singleton::ManagerImpl> singleton_manager_;
  StrictMock<Api::MockOsSysCalls> mock_posix_file_operations_;
  std::shared_ptr<AsyncFileManagerFactory> factory_;
  std::shared_ptr<AsyncFileManager> manager_;
  static constexpr absl::string_view tmpdir_{"/mocktmp"};
};

TEST_F(AsyncFileManagerWithMockFilesTest, ChainedOperationsWorkAndSkipQueue) {
  int fd = 1;
  std::promise<void> write_blocker;
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{fd, 0}));
  EXPECT_CALL(mock_posix_file_operations_, pwrite(fd, _, 5, 0))
      .WillOnce([&write_blocker](int, const void*, size_t, off_t) {
        write_blocker.get_future().wait();
        return Api::SysCallSizeResult{5, 0};
      });
  // Chain open/write/close. Write will block because of the mock expectation.
  manager_->createAnonymousFile(tmpdir_, [&](absl::StatusOr<AsyncFileHandle> result) {
    AsyncFileHandle handle = result.value();
    Buffer::OwnedImpl buf("hello");
    EXPECT_OK(handle->write(buf, 0, [handle](absl::StatusOr<size_t> result) {
      EXPECT_THAT(result, IsOkAndHolds(5U));
      EXPECT_OK(handle->close([](absl::Status result) { EXPECT_OK(result); }));
    }));
  });
  // Separately queue another action.
  std::promise<void> did_second_action;
  manager_->whenReady([&](absl::Status) { did_second_action.set_value(); });
  auto second_action_future = did_second_action.get_future();
  // Ensure that the second action didn't get a turn while the file operations are still blocking.
  EXPECT_EQ(std::future_status::timeout,
            second_action_future.wait_for(std::chrono::milliseconds(1)));
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  // Unblock the write.
  write_blocker.set_value();
  // Ensure that the second action does get a turn after the file operations completed.
  EXPECT_EQ(std::future_status::ready, second_action_future.wait_for(std::chrono::seconds(1)));
}

TEST_F(AsyncFileManagerWithMockFilesTest, CancellingAQueuedActionPreventsItFromExecuting) {
  std::promise<void> ready;
  // Add a blocking action so we can guarantee cancel is called before unlink begins.
  manager_->whenReady([&](absl::Status) { ready.get_future().wait(); });
  // Ensure that unlink doesn't get called.
  EXPECT_CALL(mock_posix_file_operations_, unlink(_)).Times(0);
  auto cancel_unlink = manager_->unlink("irrelevant", [](absl::Status) {});
  cancel_unlink();
  std::promise<void> done;
  // Add a notifying action so we can ensure that the unlink action was passed by the time
  // the test ends.
  manager_->whenReady([&](absl::Status) { done.set_value(); });
  ready.set_value();
  done.get_future().wait();
}

TEST_F(AsyncFileManagerWithMockFilesTest, CancellingACompletedActionDoesNothingImportant) {
  std::promise<void> ready;
  auto cancel = manager_->whenReady([&](absl::Status) { ready.set_value(); });
  ready.get_future().wait();
  std::this_thread::yield();
  cancel();
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CancellingDuringCreateAnonymousFileClosesFileAndPreventsCallback) {
  int fd = 1;
  std::promise<void> wait_for_open_to_be_executing;
  std::promise<void> allow_open_to_finish;
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce([&](const char*, int, int) {
        wait_for_open_to_be_executing.set_value();
        allow_open_to_finish.get_future().wait();
        return Api::SysCallIntResult{fd, 0};
      });
  std::atomic<bool> callback_was_called{false};
  // Queue opening the file, record if the callback was called (it shouldn't be).
  auto cancelOpen = manager_->createAnonymousFile(
      tmpdir_,
      [&callback_was_called](absl::StatusOr<AsyncFileHandle>) { callback_was_called.store(true); });
  // Separately queue another action.
  std::promise<bool> did_second_action;
  manager_->whenReady([&](absl::Status) { did_second_action.set_value(true); });
  // Wait for the open operation to be entered.
  wait_for_open_to_be_executing.get_future().wait();
  // Cancel the open request (but too late to actually stop it!)
  cancelOpen();
  // Expect the automatic close operation to occur.
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  // Allow the open operation to complete.
  allow_open_to_finish.set_value();
  // Ensure that the second action does get a turn after the file operation relinquishes the thread.
  // (This also ensures that the file operation reached the end, so the file should be closed now.)
  ASSERT_EQ(std::future_status::ready,
            did_second_action.get_future().wait_for(std::chrono::milliseconds(100)));
  // Ensure the callback for the open operation was *not* called, because it was cancelled.
  EXPECT_FALSE(callback_was_called.load());
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CancellingDuringOpenExistingFileClosesFileAndPreventsCallback) {
  int fd = 1;
  std::string filename = absl::StrCat(tmpdir_, "/fake_file");
  std::promise<void> wait_for_open_to_be_executing;
  std::promise<void> allow_open_to_finish;
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(filename), O_RDWR)).WillOnce([&]() {
    wait_for_open_to_be_executing.set_value();
    allow_open_to_finish.get_future().wait();
    return Api::SysCallIntResult{fd, 0};
  });
  std::atomic<bool> callback_was_called{false};
  // Queue opening the file, record if the callback was called (it shouldn't be).
  auto cancelOpen = manager_->openExistingFile(
      filename, AsyncFileManager::Mode::ReadWrite,
      [&callback_was_called](absl::StatusOr<AsyncFileHandle>) { callback_was_called.store(true); });
  // Separately queue another action.
  std::promise<bool> did_second_action;
  manager_->whenReady([&](absl::Status) { did_second_action.set_value(true); });
  // Wait for the open operation to be entered.
  wait_for_open_to_be_executing.get_future().wait();
  // Cancel the open request (but too late to actually stop it!)
  cancelOpen();
  // Expect the automatic close operation to occur.
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  // Allow the open operation to complete.
  allow_open_to_finish.set_value();
  // Ensure that the second action does get a turn after the file operation relinquishes the thread.
  // (This also ensures that the file operation reached the end, so the file should be closed now.)
  ASSERT_EQ(std::future_status::ready,
            did_second_action.get_future().wait_for(std::chrono::milliseconds(100)));
  // Ensure the callback for the open operation was *not* called, because it was cancelled.
  EXPECT_FALSE(callback_was_called.load());
}

TEST_F(AsyncFileManagerWithMockFilesTest, OpenFailureInCreateAnonymousReturnsAnError) {
  int fd = 1;
  // First do a successful open and close, to establish that we can use the O_TMPFILE path
  // (otherwise a failure will retry using 'mkstemp').
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{fd, 0}));
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  std::promise<void> first_open_was_called;
  manager_->createAnonymousFile(tmpdir_, [&](absl::StatusOr<AsyncFileHandle> result) {
    EXPECT_OK(result.value()->close([](absl::Status) {}));
    first_open_was_called.set_value();
  });
  // We have to synchronize on this to avoid racily adding a different matching expectation for the
  // first 'open'.
  first_open_was_called.get_future().wait();
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{-1, EMFILE}));
  // Capture the result of the second open call, to verify that the error code came through.
  std::promise<absl::Status> captured_result;
  manager_->createAnonymousFile(tmpdir_, [&](absl::StatusOr<AsyncFileHandle> result) {
    captured_result.set_value(result.status());
  });
  EXPECT_EQ(absl::StatusCode::kResourceExhausted, captured_result.get_future().get().code());
}

TEST_F(AsyncFileManagerWithMockFilesTest, CreateAnonymousFallbackMkstempReturnsAnErrorOnFailure) {
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
  EXPECT_CALL(mock_posix_file_operations_, mkstemp(Eq(std::string(tmpdir_) + "/buffer.XXXXXX")))
      .WillOnce(Return(Api::SysCallIntResult{-1, EMFILE}));
  // Capture the result of the open call, to verify that the error code came through.
  std::promise<absl::Status> captured_result;
  manager_->createAnonymousFile(tmpdir_, [&](absl::StatusOr<AsyncFileHandle> result) {
    captured_result.set_value(result.status());
  });
  EXPECT_EQ(absl::StatusCode::kResourceExhausted, captured_result.get_future().get().code());
}

TEST_F(AsyncFileManagerWithMockFilesTest, CreateAnonymousFallbackReturnsAnErrorIfPathTooLong) {
  std::string too_long_tmpdir{"/a_long_path"};
  while (too_long_tmpdir.size() < 4096) {
    too_long_tmpdir += too_long_tmpdir;
  }
  EXPECT_CALL(mock_posix_file_operations_,
              open(Eq(too_long_tmpdir), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
  // Capture the result of the open call, to verify that the error code came through.
  std::promise<absl::Status> captured_result;
  manager_->createAnonymousFile(too_long_tmpdir, [&](absl::StatusOr<AsyncFileHandle> result) {
    captured_result.set_value(result.status());
  });
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, captured_result.get_future().get().code());
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CreateAnonymousFallbackClosesAndReturnsAnErrorIfUnlinkWhileOpenFails) {
  int fd = 1;
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
  EXPECT_CALL(mock_posix_file_operations_, mkstemp(Eq(absl::StrCat(tmpdir_, "/buffer.XXXXXX"))))
      .WillOnce([&fd](char* tmplate) {
        memcpy(tmplate + strlen(tmplate) - 6, "ABCDEF", 6);
        return Api::SysCallIntResult{fd, 0};
      });
  // First unlink fails while the file is open, second unlink after close succeeds.
  {
    InSequence s;
    EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(absl::StrCat(tmpdir_, "/buffer.ABCDEF"))))
        .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
    EXPECT_CALL(mock_posix_file_operations_, close(fd))
        .WillOnce(Return(Api::SysCallIntResult{0, 0}));
    EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(absl::StrCat(tmpdir_, "/buffer.ABCDEF"))))
        .WillOnce(Return(Api::SysCallIntResult{0, 0}));
  }
  std::promise<absl::Status> captured_result;
  manager_->createAnonymousFile(tmpdir_, [&](absl::StatusOr<AsyncFileHandle> result) {
    captured_result.set_value(result.status());
  });
  EXPECT_EQ(absl::StatusCode::kUnimplemented, captured_result.get_future().get().code());
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CreateAnonymousFallbackToMkstempWorksAsIntendedIfNoErrorsOccur) {
  int fd = 1;
  {
    InSequence s;
    EXPECT_CALL(mock_posix_file_operations_,
                open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
        .WillOnce(Return(Api::SysCallIntResult{-1, EBADF}));
    EXPECT_CALL(mock_posix_file_operations_, mkstemp(Eq(absl::StrCat(tmpdir_, "/buffer.XXXXXX"))))
        .WillOnce([&fd](char* tmplate) {
          memcpy(tmplate + strlen(tmplate) - 6, "ABCDEF", 6);
          return Api::SysCallIntResult{fd, 0};
        });
    EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(absl::StrCat(tmpdir_, "/buffer.ABCDEF"))))
        .WillOnce(Return(Api::SysCallIntResult{0, 0}));
    EXPECT_CALL(mock_posix_file_operations_, close(fd))
        .WillOnce(Return(Api::SysCallIntResult{0, 0}));
  }
  std::promise<void> callback_complete;
  manager_->createAnonymousFile(tmpdir_, [&](absl::StatusOr<AsyncFileHandle> result) {
    EXPECT_OK(result);
    EXPECT_OK(result.value()->close([&](absl::Status result) {
      EXPECT_OK(result);
      callback_complete.set_value();
    }));
  });
  callback_complete.get_future().wait();
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
