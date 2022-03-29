#include <cerrno>
#include <memory>
#include <string>
#include <thread>

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"

#include "test/mocks/extensions/common/async_files/mock_posix_file_operations.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/barrier.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Return;

class AsyncFileManagerWithMockFilesTest : public ::testing::Test {
public:
  void SetUp() override {
    AsyncFileManagerConfig config;
    config.substitute_posix_file_operations = &mock_posix_file_operations_;
    config.thread_pool_size = 1;
    manager_ = config.createManager();
  }

protected:
  MockPosixFileOperations mock_posix_file_operations_;
  std::unique_ptr<AsyncFileManager> manager_;
  static constexpr absl::string_view tmpdir_{"/mocktmp"};
};

TEST_F(AsyncFileManagerWithMockFilesTest, ChainedOperationsWorkAndSkipQueue) {
  int fd = 1;
  absl::Barrier write_blocker{2};
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(fd));
  EXPECT_CALL(mock_posix_file_operations_, pwrite(fd, _, 5, 0))
      .WillOnce([&write_blocker](int, const void*, size_t, off_t) {
        write_blocker.Block();
        return 5;
      });
  // Chain open/write/close. Write will block because of the mock expectation.
  manager_->createAnonymousFile(tmpdir_, [](absl::StatusOr<AsyncFileHandle> result) {
    AsyncFileHandle handle = result.value();
    Envoy::Buffer::OwnedImpl buf("hello");
    handle->write(buf, 0, [handle](absl::StatusOr<size_t> result) {
      ASSERT(result.value() == 5);
      handle->close([](absl::Status result) { ASSERT(result.ok()); });
    });
  });
  // Separately queue another action.
  absl::Mutex mu;
  bool did_second_action = false;
  manager_->whenReady([&did_second_action, &mu](absl::Status) {
    absl::MutexLock lock(&mu);
    did_second_action = true;
  });
  std::this_thread::yield();
  // Ensure that the second action doesn't get a turn while the file operations are still blocking.
  {
    absl::MutexLock lock(&mu);
    EXPECT_FALSE(did_second_action);
  }
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(0));
  // Unblock the write.
  write_blocker.Block();
  // Ensure that the second action does get a turn after the file operations completed.
  {
    auto cond = [&did_second_action]() { return did_second_action; };
    absl::MutexLock lock(&mu);
    EXPECT_TRUE(mu.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(1)));
  }
}

TEST_F(AsyncFileManagerWithMockFilesTest, CancellingAQueuedActionPreventsItFromExecuting) {
  absl::Barrier ready_blocker{2};
  manager_->whenReady([&](absl::Status) { ready_blocker.Block(); });
  EXPECT_CALL(mock_posix_file_operations_, unlink(_)).Times(0);
  auto cancel_unlink = manager_->unlink("irrelevant", [](absl::Status) {});
  cancel_unlink();
  absl::Barrier done_blocker{2};
  manager_->whenReady([&](absl::Status) { done_blocker.Block(); });
  ready_blocker.Block();
  done_blocker.Block();
}

TEST_F(AsyncFileManagerWithMockFilesTest, CancellingACompletedActionDoesNothingImportant) {
  absl::Barrier ready_blocker{2};
  auto cancel = manager_->whenReady([&](absl::Status) { ready_blocker.Block(); });
  ready_blocker.Block();
  std::this_thread::yield();
  cancel();
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CancellingDuringCreateAnonymousFileClosesFileAndPreventsCallback) {
  int fd = 1;
  absl::Barrier open_blocker{2};
  absl::Barrier open_blocker_finish{2};
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce([&open_blocker, &open_blocker_finish, &fd](const char*, int, int) {
        open_blocker.Block();
        open_blocker_finish.Block();
        return fd;
      });
  std::atomic<bool> callback_was_called{false};
  // Queue opening the file, record if the callback was called (it shouldn't be).
  auto cancelOpen = manager_->createAnonymousFile(
      tmpdir_,
      [&callback_was_called](absl::StatusOr<AsyncFileHandle>) { callback_was_called.store(true); });
  // Separately queue another action.
  absl::Mutex mu;
  bool did_second_action = false;
  manager_->whenReady([&did_second_action, &mu](absl::Status) {
    absl::MutexLock lock(&mu);
    did_second_action = true;
  });
  // Wait for the open operation to be entered.
  open_blocker.Block();
  // Cancel the open request (but too late to actually stop it!)
  cancelOpen();
  // Expect the automatic close operation to occur.
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(0));
  // Allow the open operation to complete.
  open_blocker_finish.Block();
  // Ensure that the second action does get a turn after the file operation relinquishes the thread.
  // (This also ensures that the file operation reached the end, so the file should be closed now.)
  {
    auto cond = [&did_second_action]() { return did_second_action; };
    absl::MutexLock lock(&mu);
    EXPECT_TRUE(mu.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(1)));
  }
  // Ensure the callback for the open operation was *not* called, because it was cancelled.
  EXPECT_FALSE(callback_was_called.load());
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CancellingDuringOpenExistingFileClosesFileAndPreventsCallback) {
  int fd = 1;
  std::string filename = absl::StrCat(tmpdir_, "/fake_file");
  absl::Barrier open_blocker{2};
  absl::Barrier open_blocker_finish{2};
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(filename), O_RDWR))
      .WillOnce([&open_blocker, &open_blocker_finish, &fd]() {
        open_blocker.Block();
        open_blocker_finish.Block();
        return fd;
      });
  std::atomic<bool> callback_was_called{false};
  // Queue opening the file, record if the callback was called (it shouldn't be).
  auto cancelOpen = manager_->openExistingFile(
      filename, AsyncFileManager::Mode::ReadWrite,
      [&callback_was_called](absl::StatusOr<AsyncFileHandle>) { callback_was_called.store(true); });
  // Separately queue another action.
  absl::Mutex mu;
  bool did_second_action = false;
  manager_->whenReady([&did_second_action, &mu](absl::Status) {
    absl::MutexLock lock(&mu);
    did_second_action = true;
  });
  // Wait for the open operation to be entered.
  open_blocker.Block();
  // Cancel the open request (but too late to actually stop it!)
  cancelOpen();
  // Expect the automatic close operation to occur.
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(0));
  // Allow the open operation to complete.
  open_blocker_finish.Block();
  // Ensure that the second action does get a turn after the file operation relinquishes the thread.
  // (This also ensures that the file operation reached the end, so the file should be closed now.)
  {
    auto cond = [&did_second_action]() { return did_second_action; };
    absl::MutexLock lock(&mu);
    EXPECT_TRUE(mu.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(1)));
  }
  // Ensure the callback for the open operation was *not* called, because it was cancelled.
  EXPECT_FALSE(callback_was_called.load());
}

TEST_F(AsyncFileManagerWithMockFilesTest, OpenFailureInCreateAnonymousReturnsAnError) {
  int fd = 1;
  // First do a successful open and close, to establish that we can use the O_TMPFILE path
  // (otherwise a failure will retry using 'mkstemp').
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(fd));
  EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(0));
  absl::Barrier first_open_blocker{2};
  manager_->createAnonymousFile(tmpdir_,
                                [&first_open_blocker](absl::StatusOr<AsyncFileHandle> result) {
                                  result.value()->close([](absl::Status) {});
                                  first_open_blocker.Block();
                                });
  // We have to synchronize on this to avoid racily adding a different matching expectation for the
  // first 'open'.
  first_open_blocker.Block();
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce([](const char*, int, int) {
        errno = EMFILE;
        return -1;
      });
  // Capture the result of the second open call, to verify that the error code came through.
  absl::Status captured_result;
  absl::Barrier callback_blocker{2};
  manager_->createAnonymousFile(
      tmpdir_, [&callback_blocker, &captured_result](absl::StatusOr<AsyncFileHandle> result) {
        captured_result = result.status();
        callback_blocker.Block();
      });
  callback_blocker.Block();
  EXPECT_EQ(absl::StatusCode::kResourceExhausted, captured_result.code());
}

TEST_F(AsyncFileManagerWithMockFilesTest, CreateAnonymousFallbackMkstempReturnsAnErrorOnFailure) {
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_posix_file_operations_, mkstemp(Eq(std::string(tmpdir_) + "/buffer.XXXXXX")))
      .WillOnce([]() {
        errno = EMFILE;
        return -1;
      });
  // Capture the result of the open call, to verify that the error code came through.
  absl::Status captured_result;
  absl::Barrier callback_blocker{2};
  manager_->createAnonymousFile(
      tmpdir_, [&callback_blocker, &captured_result](absl::StatusOr<AsyncFileHandle> result) {
        captured_result = result.status();
        callback_blocker.Block();
      });
  callback_blocker.Block();
  EXPECT_EQ(absl::StatusCode::kResourceExhausted, captured_result.code());
}

TEST_F(AsyncFileManagerWithMockFilesTest, CreateAnonymousFallbackReturnsAnErrorIfPathTooLong) {
  std::string too_long_tmpdir{"/a_long_path"};
  while (too_long_tmpdir.size() < 4096) {
    too_long_tmpdir += too_long_tmpdir;
  }
  EXPECT_CALL(mock_posix_file_operations_,
              open(Eq(too_long_tmpdir), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(-1));
  // Capture the result of the open call, to verify that the error code came through.
  absl::Status captured_result;
  absl::Barrier callback_blocker{2};
  manager_->createAnonymousFile(too_long_tmpdir, [&callback_blocker, &captured_result](
                                                     absl::StatusOr<AsyncFileHandle> result) {
    captured_result = result.status();
    callback_blocker.Block();
  });
  callback_blocker.Block();
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, captured_result.code());
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CreateAnonymousFallbackClosesAndReturnsAnErrorIfUnlinkWhileOpenFails) {
  int fd = 1;
  EXPECT_CALL(mock_posix_file_operations_, open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_posix_file_operations_, mkstemp(Eq(absl::StrCat(tmpdir_, "/buffer.XXXXXX"))))
      .WillOnce([&fd](char* tmplate) {
        memcpy(tmplate + strlen(tmplate) - 6, "ABCDEF", 6);
        return fd;
      });
  // First unlink fails while the file is open, second unlink after close succeeds.
  {
    InSequence s;
    EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(absl::StrCat(tmpdir_, "/buffer.ABCDEF"))))
        .WillOnce(Return(-1));
    EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(0));
    EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(absl::StrCat(tmpdir_, "/buffer.ABCDEF"))))
        .WillOnce(Return(0));
  }
  absl::Status captured_result;
  absl::Barrier callback_blocker{2};
  manager_->createAnonymousFile(
      tmpdir_, [&callback_blocker, &captured_result](absl::StatusOr<AsyncFileHandle> result) {
        captured_result = result.status();
        callback_blocker.Block();
      });
  callback_blocker.Block();
  EXPECT_EQ(absl::StatusCode::kUnimplemented, captured_result.code());
}

TEST_F(AsyncFileManagerWithMockFilesTest,
       CreateAnonymousFallbackToMkstempWorksAsIntendedIfNoErrorsOccur) {
  int fd = 1;
  {
    InSequence s;
    EXPECT_CALL(mock_posix_file_operations_,
                open(Eq(tmpdir_), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR))
        .WillOnce(Return(-1));
    EXPECT_CALL(mock_posix_file_operations_, mkstemp(Eq(absl::StrCat(tmpdir_, "/buffer.XXXXXX"))))
        .WillOnce([&fd](char* tmplate) {
          memcpy(tmplate + strlen(tmplate) - 6, "ABCDEF", 6);
          return fd;
        });
    EXPECT_CALL(mock_posix_file_operations_, unlink(Eq(absl::StrCat(tmpdir_, "/buffer.ABCDEF"))))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_posix_file_operations_, close(fd)).WillOnce(Return(0));
  }
  absl::Barrier callback_blocker{2};
  manager_->createAnonymousFile(tmpdir_,
                                [&callback_blocker](absl::StatusOr<AsyncFileHandle> result) {
                                  ASSERT(result.ok());
                                  result.value()->close([&callback_blocker](absl::Status result) {
                                    ASSERT(result.ok());
                                    callback_blocker.Block();
                                  });
                                });
  callback_blocker.Block();
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
